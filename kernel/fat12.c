/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — FAT12 File System (read-only)
 *
 * Reads files from the root directory of the boot floppy. The layout
 * comes from the BIOS Parameter Block in the boot sector. The whole
 * FAT (a few kilobytes) is loaded at mount time; directory and data
 * sectors are read on demand through one shared sector buffer, so
 * every public function holds the file system mutex.
 */

#include "fat12.h"
#include "disk.h"
#include "memory.h"
#include "sync.h"

#define DIR_ENTRY_SIZE      32
#define DIR_ENTRIES_PER_SECTOR (DISK_SECTOR_SIZE / DIR_ENTRY_SIZE)

#define ATTR_VOLUME         0x08
#define ATTR_DIRECTORY      0x10
#define ATTR_LONG_NAME      0x0F

#define ENTRY_END           0x00
#define ENTRY_DELETED       0xE5

#define FAT12_BAD           0xFF7   /* Values from here up end a chain */
#define FAT12_MAX_CLUSTERS  4084

/* ── Volume state ───────────────────────────── */

static bool     mounted;
static uint8_t  sectors_per_cluster;
static uint16_t root_entries;
static uint16_t root_lba;
static uint16_t data_lba;
static uint16_t cluster_count;
static uint8_t *fat;                /* The first FAT, loaded whole */

static uint8_t  sector[DISK_SECTOR_SIZE];
static mutex_t  lock;
static bool     lock_ready;

static uint16_t get16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

static void fs_lock(void)
{
    if (!lock_ready) {
        mutex_init(&lock);
        lock_ready = true;
    }
    mutex_lock(&lock);
}

/* ── Cluster chain ──────────────────────────── */

/* FAT12 packs two 12-bit entries into three bytes */
static uint16_t fat_entry(uint16_t cluster)
{
    uint16_t offset = cluster + cluster / 2;
    uint16_t value = get16(&fat[offset]);

    return (cluster & 1) ? value >> 4 : value & 0x0FFF;
}

static bool cluster_valid(uint16_t cluster)
{
    return cluster >= 2 && cluster < cluster_count + 2;
}

/* ── Mount ──────────────────────────────────── */

bool fat_mount(void)
{
    uint16_t reserved, sectors_per_fat, total, i;
    uint8_t fats;
    bool ok = false;

    fs_lock();
    mounted = false;

    if (!disk_read_sector(0, sector)) goto done;
    if (sector[510] != 0x55 || sector[511] != 0xAA) goto done;
    if (get16(&sector[11]) != DISK_SECTOR_SIZE) goto done;

    sectors_per_cluster = sector[13];
    reserved            = get16(&sector[14]);
    fats                = sector[16];
    root_entries        = get16(&sector[17]);
    total               = get16(&sector[19]);
    sectors_per_fat     = get16(&sector[22]);

    if (sectors_per_cluster == 0 || fats == 0 || root_entries == 0 ||
        sectors_per_fat == 0 || total == 0) goto done;

    disk_set_geometry(get16(&sector[24]), get16(&sector[26]));

    root_lba = reserved + fats * sectors_per_fat;
    data_lba = root_lba + (root_entries + DIR_ENTRIES_PER_SECTOR - 1) / DIR_ENTRIES_PER_SECTOR;
    if (data_lba >= total) goto done;

    cluster_count = (total - data_lba) / sectors_per_cluster;
    if (cluster_count > FAT12_MAX_CLUSTERS) goto done;      /* That would be FAT16 */

    /* The FAT must be able to describe every cluster */
    if ((uint32_t)sectors_per_fat * DISK_SECTOR_SIZE < ((uint32_t)cluster_count + 2) * 3 / 2 + 1) goto done;

    if (fat == NULL) {
        fat = kmalloc(sectors_per_fat * DISK_SECTOR_SIZE);
        if (fat == NULL) goto done;
    }
    for (i = 0; i < sectors_per_fat; i++) {
        if (!disk_read_sector(reserved + i, fat + i * DISK_SECTOR_SIZE)) goto done;
    }

    mounted = true;
    ok = true;

done:
    mutex_unlock(&lock);
    return ok;
}

bool fat_mounted(void)
{
    return mounted;
}

/* ── Directory ──────────────────────────────── */

/* "README  TXT" → "README.TXT" */
static void format_name(const uint8_t *raw, char *out)
{
    uint8_t i;

    for (i = 0; i < 8 && raw[i] != ' '; i++) {
        *out++ = (char)raw[i];
    }
    if (raw[8] != ' ') {
        *out++ = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++) {
            *out++ = (char)raw[i];
        }
    }
    *out = '\0';
}

/* Unlocked directory walk; see fat_next */
static bool next_entry(uint16_t *index, fat_dirent_t *entry)
{
    while (*index < root_entries) {
        uint16_t slot = *index % DIR_ENTRIES_PER_SECTOR;
        const uint8_t *raw;

        if (!disk_read_sector(root_lba + *index / DIR_ENTRIES_PER_SECTOR, sector)) {
            return false;
        }
        raw = &sector[slot * DIR_ENTRY_SIZE];
        (*index)++;

        if (raw[0] == ENTRY_END) {
            return false;       /* No entries after this one */
        }
        if (raw[0] == ENTRY_DELETED ||
            (raw[11] & ATTR_LONG_NAME) == ATTR_LONG_NAME ||
            (raw[11] & (ATTR_VOLUME | ATTR_DIRECTORY))) {
            continue;
        }

        format_name(raw, entry->name);
        entry->first_cluster = get16(&raw[26]);
        entry->size = get16(&raw[28]) | ((uint32_t)get16(&raw[30]) << 16);
        return true;
    }
    return false;
}

bool fat_next(uint16_t *index, fat_dirent_t *entry)
{
    bool found = false;

    fs_lock();
    if (mounted) {
        found = next_entry(index, entry);
    }
    mutex_unlock(&lock);
    return found;
}

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

static bool name_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (upper(*a) != upper(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool fat_open(const char *name, fat_file_t *file)
{
    fat_dirent_t entry;
    uint16_t index = 0;
    bool found = false;

    fs_lock();
    while (mounted && !found && next_entry(&index, &entry)) {
        if (name_equal(name, entry.name)) {
            file->size = entry.size;
            file->position = 0;
            file->cluster = entry.first_cluster;
            found = true;
        }
    }
    mutex_unlock(&lock);
    return found;
}

/* ── Read ───────────────────────────────────── */

uint16_t fat_read(fat_file_t *file, uint8_t *buffer, uint16_t length)
{
    uint16_t cluster_bytes;
    uint16_t done = 0;

    fs_lock();
    cluster_bytes = (uint16_t)sectors_per_cluster * DISK_SECTOR_SIZE;

    while (mounted && done < length && file->position < file->size) {
        uint16_t in_cluster = (uint16_t)(file->position % cluster_bytes);
        uint16_t in_sector = in_cluster % DISK_SECTOR_SIZE;
        uint16_t chunk = DISK_SECTOR_SIZE - in_sector;
        uint32_t left = file->size - file->position;
        uint16_t lba, i;

        if (!cluster_valid(file->cluster)) break;      /* Chain ended early */

        if (chunk > length - done) chunk = length - done;
        if (chunk > left) chunk = (uint16_t)left;

        lba = data_lba + (file->cluster - 2) * sectors_per_cluster + in_cluster / DISK_SECTOR_SIZE;
        if (!disk_read_sector(lba, sector)) break;

        for (i = 0; i < chunk; i++) {
            buffer[done + i] = sector[in_sector + i];
        }
        done += chunk;
        file->position += chunk;

        /* Crossed into the next cluster: follow the chain */
        if (in_cluster + chunk == cluster_bytes) {
            uint16_t next = fat_entry(file->cluster);
            file->cluster = (next >= FAT12_BAD) ? 0 : next;
        }
    }

    mutex_unlock(&lock);
    return done;
}
