/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Memory Manager Header
 *
 * Two allocators:
 *   Near heap  — inside the kernel data segment, byte-granular,
 *                reached with ordinary (near) pointers.
 *   Far arena  — conventional memory above the kernel segments,
 *                paragraph-granular (16 bytes), addressed by segment.
 */

#ifndef PHOENIX_MEMORY_H
#define PHOENIX_MEMORY_H

#include "../include/types.h"

/* Initialize both allocators */
void mem_init(void);

/* ── Near heap ──────────────────────────────── */

/* Allocate a block of memory (first-fit) */
void *kmalloc(uint16_t size);

/* Free a previously allocated block */
void kfree(void *ptr);

/* Get total free memory in the heap */
uint16_t mem_free(void);

/* Get total used memory in the heap */
uint16_t mem_used(void);

/* ── Far arena ──────────────────────────────── */

/*
 * Allocate a block of the given number of 16-byte paragraphs.
 * Returns the segment of the block (data at segment:0000), or 0.
 */
uint16_t far_alloc(uint16_t paragraphs);

/* Free a block returned by far_alloc */
void far_free(uint16_t segment);

/* Free paragraphs in the far arena */
uint16_t far_free_paras(void);

/* Allocator bounds, for diagnostics and telemetry */
typedef struct {
    uint16_t heap_start;    /* Near heap: first byte (offset in the data segment) */
    uint16_t heap_end;      /* Near heap: one past the last byte */
    uint16_t far_start;     /* Far arena: first segment (0 if none) */
    uint16_t far_end;       /* Far arena: one past the last segment */
} mem_layout_t;

void mem_get_layout(mem_layout_t *layout);

/* Print memory map to console */
void mem_print_map(void);

#endif /* PHOENIX_MEMORY_H */
