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

/* Print memory map to console */
void mem_print_map(void);

#endif /* PHOENIX_MEMORY_H */
