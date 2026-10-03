/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Program Loader
 *
 * Loads a program built with the SDK (file format: sdk/mkprog.py) and
 * runs it as a thread.
 *
 *   text  → its own segment in far memory; the thread runs with CS
 *           pointing there, so code offsets need no adjustment
 *   data  → a block on the kernel's near heap, followed by the zeroed
 *           bss. The program keeps DS = SS = the kernel data segment,
 *           like every other thread, so the interrupt and system call
 *           paths are unchanged and pointers passed to system calls
 *           are ordinary near pointers.
 *
 * Because the data block can land anywhere on the heap, the file lists
 * every 16-bit data address in the program; the loader adds the
 * block's address to each (relocation).
 *
 * The thread owns both allocations; they are freed when it ends.
 * Real mode has no protection: a program can overwrite the kernel.
 */

#include "exec.h"
#include "fat12.h"
#include "memory.h"
#include "thread.h"
#include "hal.h"

#define HEADER_SIZE     16
#define CHUNK           256     /* Bytes read from the file at a time */

typedef struct {
    uint16_t text_size;
    uint16_t data_size;
    uint16_t bss_size;
    uint16_t entry;
    uint16_t text_relocs;
    uint16_t data_relocs;
} header_t;

static uint16_t get16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

/* Derive a thread name from the file name: "HELLO.BIN" → "hello" */
static void program_name(const char *file, char *out)
{
    uint8_t i;

    for (i = 0; i < 11 && file[i] && file[i] != '.'; i++) {
        char c = file[i];
        out[i] = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
    }
    out[i] = '\0';
}

static bool read_exact(fat_file_t *file, uint8_t *buffer, uint16_t length)
{
    return fat_read(file, buffer, length) == length;
}

/* Read `size` bytes of the file into far memory at segment:0 */
static bool read_far(fat_file_t *file, uint16_t segment, uint16_t size, uint8_t *chunk)
{
    uint8_t __far *dest = (uint8_t __far *)MK_FP(segment, 0);
    uint16_t done = 0;

    while (done < size) {
        uint16_t part = size - done;
        uint16_t i;

        if (part > CHUNK) part = CHUNK;
        if (!read_exact(file, chunk, part)) return false;
        for (i = 0; i < part; i++) {
            dest[done + i] = chunk[i];
        }
        done += part;
    }
    return true;
}

/*
 * Apply one relocation table. Each entry is the offset of a 16-bit
 * word, inside a region `limit` bytes long, that holds a data address.
 */
static uint8_t relocate(fat_file_t *file, uint16_t count, uint16_t limit, uint8_t *chunk,
                        uint16_t text_segment, uint8_t *data, bool in_text)
{
    while (count > 0) {
        uint16_t batch = count > CHUNK / 2 ? CHUNK / 2 : count;
        uint16_t i;

        if (!read_exact(file, chunk, batch * 2)) return EXEC_READ_ERROR;

        for (i = 0; i < batch; i++) {
            uint16_t offset = get16(&chunk[i * 2]);

            if (limit < 2 || offset > limit - 2) return EXEC_BAD_FORMAT;

            if (in_text) {
                uint8_t __far *word = (uint8_t __far *)MK_FP(text_segment, offset);
                uint16_t value = (word[0] | ((uint16_t)word[1] << 8)) + (uint16_t)data;
                word[0] = (uint8_t)value;
                word[1] = (uint8_t)(value >> 8);
            } else {
                uint16_t value = get16(&data[offset]) + (uint16_t)data;
                data[offset] = (uint8_t)value;
                data[offset + 1] = (uint8_t)(value >> 8);
            }
        }
        count -= batch;
    }
    return EXEC_OK;
}

int exec_program(const char *name, uint8_t *error)
{
    uint8_t raw[HEADER_SIZE];
    char thread_name[12];
    fat_file_t file;
    header_t header;
    uint16_t text_segment = 0;
    uint8_t *data = NULL;
    uint8_t *chunk = NULL;
    uint16_t data_total, i;
    uint8_t status = EXEC_OK;
    int tid = -1;

    if (!fat_open(name, &file)) {
        status = EXEC_NOT_FOUND;
        goto done;
    }

    if (!read_exact(&file, raw, HEADER_SIZE) ||
        raw[0] != 'P' || raw[1] != 'X' || raw[2] != 'E' || raw[3] != '1') {
        status = EXEC_BAD_FORMAT;
        goto done;
    }
    header.text_size   = get16(&raw[4]);
    header.data_size   = get16(&raw[6]);
    header.bss_size    = get16(&raw[8]);
    header.entry       = get16(&raw[10]);
    header.text_relocs = get16(&raw[12]);
    header.data_relocs = get16(&raw[14]);

    data_total = header.data_size + header.bss_size;

    /* The sizes must be consistent with each other and with the file */
    if (header.text_size == 0 || header.entry >= header.text_size ||
        data_total < header.data_size ||
        (uint32_t)HEADER_SIZE + header.text_size + header.data_size +
            ((uint32_t)header.text_relocs + header.data_relocs) * 2 != file.size) {
        status = EXEC_BAD_FORMAT;
        goto done;
    }

    chunk = kmalloc(CHUNK);
    text_segment = far_alloc((header.text_size + 15) / 16);
    /* Always allocate a data block, so relocated addresses are never NULL */
    data = kmalloc(data_total ? data_total : 2);
    if (chunk == NULL || text_segment == 0 || data == NULL) {
        status = EXEC_NO_MEMORY;
        goto done;
    }

    if (!read_far(&file, text_segment, header.text_size, chunk) ||
        !read_exact(&file, data, header.data_size)) {
        status = EXEC_READ_ERROR;
        goto done;
    }
    for (i = header.data_size; i < data_total; i++) {
        data[i] = 0;
    }

    status = relocate(&file, header.text_relocs, header.text_size, chunk, text_segment, data, true);
    if (status == EXEC_OK) {
        status = relocate(&file, header.data_relocs, header.data_size, chunk, text_segment, data, false);
    }
    if (status != EXEC_OK) {
        goto done;
    }

    program_name(name, thread_name);
    tid = thread_create_program(text_segment, header.entry, data, EXEC_PRIORITY, thread_name);
    if (tid < 0) {
        status = EXEC_NO_THREAD;
    }

done:
    kfree(chunk);
    if (tid < 0) {
        /* The thread never took ownership */
        far_free(text_segment);
        kfree(data);
    }
    if (error) {
        *error = status;
    }
    return tid;
}

const char *exec_error_text(uint8_t error)
{
    static const char *const text[] = {
        "ok", "file not found", "not a Phoenix-8086 program", "out of memory",
        "disk read error", "no free thread slots"
    };

    return error < sizeof(text) / sizeof(text[0]) ? text[error] : "unknown error";
}
