/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Program Loader
 *
 * Loads a program built with the SDK (file format: sdk/mkprog.py) and
 * runs it as a thread.
 *
 * A program gets two segments of far memory:
 *
 *   code  → its text. The thread runs with CS pointing here.
 *   data  → its initialised data, its zeroed bss, and the stacks of
 *           its threads. The thread runs with DS = ES = SS pointing
 *           here, so the program's pointers are offsets into its own
 *           segment and it cannot run out of the kernel's heap.
 *
 * Both parts are linked at fixed offsets within their segments, so
 * nothing needs adjusting at load time. Data starts at PROG_DATA_START
 * rather than 0, leaving address 0 unused: a null pointer never points
 * at anything.
 *
 * The program's memory is shared by every thread running in it (a
 * program can start more with the thread_create system call) and is
 * freed when the last of them ends.
 * Real mode has no protection: a program can still overwrite the kernel.
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
    uint16_t data_start;
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

/* Read `size` bytes of the file into far memory at segment:offset */
static bool read_far(fat_file_t *file, uint16_t segment, uint16_t offset, uint16_t size,
                     uint8_t *chunk)
{
    uint8_t __far *dest = (uint8_t __far *)MK_FP(segment, offset);
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

int exec_program(const char *name, uint8_t *error)
{
    uint8_t raw[HEADER_SIZE];
    char thread_name[12];
    fat_file_t file;
    header_t header;
    program_t *program = NULL;
    uint16_t code_segment = 0;
    uint16_t data_segment = 0;
    uint8_t *chunk = NULL;
    uint32_t data_end;
    uint16_t stack_area, i;
    uint8_t status = EXEC_OK;
    int tid = -1;

    if (!fat_open(name, &file)) {
        status = EXEC_NOT_FOUND;
        goto done;
    }

    if (!read_exact(&file, raw, HEADER_SIZE) ||
        raw[0] != 'P' || raw[1] != 'X' || raw[2] != 'E' || raw[3] != '2') {
        status = EXEC_BAD_FORMAT;
        goto done;
    }
    header.text_size  = get16(&raw[4]);
    header.data_size  = get16(&raw[6]);
    header.bss_size   = get16(&raw[8]);
    header.entry      = get16(&raw[10]);
    header.data_start = get16(&raw[12]);

    /* Data, bss and the thread stacks must all fit in one 64 KB segment */
    data_end = (uint32_t)PROG_DATA_START + header.data_size + header.bss_size;
    stack_area = (uint16_t)((data_end + 1) & ~1UL);

    /* The sizes must be consistent with each other and with the file */
    if (header.text_size == 0 || header.entry >= header.text_size ||
        header.data_start != PROG_DATA_START ||
        data_end + (uint32_t)PROG_MAX_THREADS * PROG_STACK_SIZE > 0xFFF0UL ||
        (uint32_t)HEADER_SIZE + header.text_size + header.data_size != file.size) {
        status = EXEC_BAD_FORMAT;
        goto done;
    }

    chunk = kmalloc(CHUNK);
    program = kmalloc(sizeof(program_t));
    code_segment = far_alloc((header.text_size + 15) / 16);
    data_segment = far_alloc((stack_area + PROG_MAX_THREADS * PROG_STACK_SIZE + 15) / 16);
    if (chunk == NULL || program == NULL || code_segment == 0 || data_segment == 0) {
        status = EXEC_NO_MEMORY;
        goto done;
    }

    if (!read_far(&file, code_segment, 0, header.text_size, chunk) ||
        !read_far(&file, data_segment, PROG_DATA_START, header.data_size, chunk)) {
        status = EXEC_READ_ERROR;
        goto done;
    }

    /* Zero the unused bytes below the data, and the bss above it */
    {
        uint8_t __far *data = (uint8_t __far *)MK_FP(data_segment, 0);

        for (i = 0; i < PROG_DATA_START; i++) {
            data[i] = 0;
        }
        for (i = PROG_DATA_START + header.data_size; i < stack_area; i++) {
            data[i] = 0;
        }
    }

    program->code_segment  = code_segment;
    program->data_segment  = data_segment;
    program->text_size     = header.text_size;
    program->data_limit    = stack_area + PROG_MAX_THREADS * PROG_STACK_SIZE;
    program->stack_area    = stack_area;
    program->threads       = 0;
    program->stacks_in_use = 0;

    program_name(name, thread_name);
    tid = thread_create_program(program, header.entry, 0, EXEC_PRIORITY, thread_name);
    if (tid < 0) {
        status = EXEC_NO_THREAD;
    }

done:
    kfree(chunk);
    if (tid < 0) {
        /* No thread took a reference */
        far_free(code_segment);
        far_free(data_segment);
        kfree(program);
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
