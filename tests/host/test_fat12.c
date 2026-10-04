/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Host tests for kernel/fat12.c
 *
 *     test_fat12 IMAGE FILE...
 *
 * Mounts the real boot floppy through a disk layer that reads the
 * image file, lists the root directory, and reads every FILE named on
 * the command line back through the driver, comparing it byte for
 * byte with the host's copy. FILE names are matched by their 8.3 name.
 *
 * Then it fuzzes: thousands of copies of the image with random bytes
 * changed in the boot sector, the FAT and the root directory. The
 * driver must mount or refuse each one, list it, and read every file
 * on it without crashing, reading outside the disk or looping forever.
 * Run under AddressSanitizer and UBSan, an out-of-bounds access in the
 * parser is a failure even if it would go unnoticed on the 8086.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "machine.h"
#include "../../kernel/fat12.h"
#include "../../kernel/disk.h"
#include "../../kernel/memory.h"

#ifndef FUZZ_ROUNDS
#define FUZZ_ROUNDS     3000        /* make test-host; more with -DFUZZ_ROUNDS=... */
#endif
#define FUZZ_AREA       (40 * DISK_SECTOR_SIZE)     /* boot sector, FATs, root directory */

static unsigned char *disk;         /* The image the driver sees */
static long disk_size;
static unsigned long sector_reads;

/* ── The disk layer and allocator fat12.c uses ── */

bool disk_read_sector(uint16_t lba, uint8_t *buffer)
{
    sector_reads++;
    if ((long)(lba + 1) * DISK_SECTOR_SIZE > disk_size) {
        return false;               /* Past the end of the disk: a read error */
    }
    memcpy(buffer, disk + (long)lba * DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
    return true;
}

void disk_set_geometry(uint16_t sectors_per_track, uint16_t heads)
{
    (void)sectors_per_track;
    (void)heads;
}

void *kmalloc(uint16_t size)
{
    return malloc(size);
}

void kfree(void *ptr)
{
    free(ptr);
}

/* ── Helpers ────────────────────────────────── */

static unsigned char *load(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;

    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)*size + 1);
    if (fread(data, 1, (size_t)*size, f) != (size_t)*size) {
        perror(path);
        exit(2);
    }
    fclose(f);
    return data;
}

/* "build/programs/HELLO.BIN" → "HELLO.BIN" */
static void short_name(const char *path, char *out)
{
    const char *base = strrchr(path, '/');
    size_t i;

    base = base ? base + 1 : path;
    for (i = 0; base[i] && i < FAT_NAME_MAX - 1; i++) {
        out[i] = (char)toupper((unsigned char)base[i]);
    }
    out[i] = '\0';
}

/* Read a whole file through the driver in odd-sized pieces */
static long read_all(const char *name, unsigned char *out, long limit)
{
    fat_file_t file;
    long total = 0;
    uint16_t got;
    uint16_t piece = 37;

    if (!fat_open(name, &file)) return -1;
    while (total < limit && (got = fat_read(&file, out + total, piece)) > 0) {
        total += got;
        piece = (uint16_t)(piece * 3 % 700 + 1);
    }
    return total;
}

/* ── Tests ──────────────────────────────────── */

static void test_real_image(int count, char **files)
{
    uint16_t index = 0;
    fat_dirent_t entry;
    int listed = 0;
    int i;

    CHECK(fat_mount());
    CHECK(fat_mounted());

    while (fat_next(&index, &entry)) {
        listed++;
    }
    CHECK(listed == count);

    for (i = 0; i < count; i++) {
        char name[FAT_NAME_MAX];
        long expected_size, got;
        unsigned char *expected = load(files[i], &expected_size);
        unsigned char *buffer = malloc((size_t)expected_size + 512);

        short_name(files[i], name);
        got = read_all(name, buffer, expected_size + 512);
        if (got != expected_size || memcmp(buffer, expected, (size_t)expected_size) != 0) {
            fprintf(stderr, "  %s: read %ld bytes, expected %ld\n", name, got, expected_size);
            CHECK(!"file read back through the driver matches the host copy");
        } else {
            CHECK(true);
        }
        free(buffer);
        free(expected);
    }

    {
        fat_file_t file;
        CHECK(!fat_open("MISSING.BIN", &file));
        CHECK(fat_open("readme.txt", &file));       /* case-insensitive */
    }
}

static void put16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

/*
 * The three bugs the fuzzer found in fat_mount, one case each. On the
 * 8086 none of them would have been noticed until the heap or a
 * division went wrong.
 */
static void test_bpb_limits(const unsigned char *pristine)
{
    fat_file_t file;
    unsigned char buffer[64];

    /* 128 sectors per FAT: 128 * 512 wrapped to 0 in 16 bits; the FAT was read into a tiny block */
    memcpy(disk, pristine, (size_t)disk_size);
    put16(&disk[22], 128);
    CHECK(!fat_mount());

    /* 128 sectors per cluster: the cluster size wrapped to 0 and fat_read divided by it */
    memcpy(disk, pristine, (size_t)disk_size);
    disk[13] = 128;
    CHECK(!fat_mount());

    /* Remounting a disk with a bigger FAT than the first: the old buffer was reused */
    memcpy(disk, pristine, (size_t)disk_size);
    CHECK(fat_mount());
    put16(&disk[22], 12);               /* still a valid FAT12 layout, just a bigger FAT */
    CHECK(fat_mount());                 /* ASan reports the overflow if the buffer is reused */
    memcpy(disk, pristine, (size_t)disk_size);
    CHECK(fat_mount());
    CHECK(fat_open("README.TXT", &file) && fat_read(&file, buffer, sizeof(buffer)) > 0);
}

static void test_fuzz(const unsigned char *pristine)
{
    unsigned round;
    unsigned mounted = 0, refused = 0;
    unsigned char *buffer = malloc(1 << 20);
    /* Where the pristine image keeps its FAT and root directory */
    long reserved = pristine[14] | pristine[15] << 8;
    long per_fat = pristine[22] | pristine[23] << 8;
    long fat_start = reserved * DISK_SECTOR_SIZE;
    long fat_bytes = per_fat * DISK_SECTOR_SIZE;
    long root_start = fat_start + pristine[16] * fat_bytes;
    long root_bytes = (long)(pristine[17] | pristine[18] << 8) * 32;

    srand(1978);
    for (round = 0; round < FUZZ_ROUNDS; round++) {
        uint16_t index = 0;
        fat_dirent_t entry;
        int changes = 1 + rand() % 8;
        int listed = 0;

        memcpy(disk, pristine, (size_t)disk_size);
        while (changes--) {
            /*
             * Aim: the boot sector's parameter block (where a single
             * byte changes the whole layout), the FAT, the root
             * directory, or anywhere in the first 40 sectors.
             */
            long at;
            switch (rand() % 4) {
            case 0:  at = 11 + rand() % 25; break;
            case 1:  at = fat_start + rand() % fat_bytes; break;
            case 2:  at = root_start + rand() % root_bytes; break;
            default: at = rand() % FUZZ_AREA; break;
            }
            disk[at] = (rand() % 4 == 0) ? (unsigned char)(1u << (rand() % 8)) ^ disk[at]
                                         : (unsigned char)rand();
        }

        if (!fat_mount()) {
            refused++;
            continue;
        }
        mounted++;
        while (listed < 1000 && fat_next(&index, &entry)) {
            /* Every file the directory claims must be readable without harm */
            if (entry.size < (1 << 20)) {
                read_all(entry.name, buffer, 1 << 20);
            }
            listed++;
        }
        CHECK(listed < 1000);       /* the directory walk ends */
    }
    free(buffer);
    printf("  fuzz     %u corrupted images: %u mounted and walked, %u refused\n",
           FUZZ_ROUNDS, mounted, refused);
    CHECK(mounted > 0);
}

int main(int argc, char **argv)
{
    unsigned char *pristine;

    if (argc < 2) {
        fprintf(stderr, "usage: %s IMAGE FILE...\n", argv[0]);
        return 2;
    }
    host_reset();
    pristine = load(argv[1], &disk_size);
    disk = malloc((size_t)disk_size);
    memcpy(disk, pristine, (size_t)disk_size);

    test_real_image(argc - 2, argv + 2);
    test_bpb_limits(pristine);
    test_fuzz(pristine);

    free(disk);
    free(pristine);
    return host_summary("fat12");
}
