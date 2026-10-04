/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Host tests for kernel/memory.c
 *
 * The file is included rather than linked so the tests can see its
 * private state (the free list, the far arena bounds) and check the
 * allocators' invariants after every operation:
 *
 *   near heap  the free list is in address order, blocks do not
 *              overlap or touch (touching blocks must have been
 *              merged), and free + allocated = the whole heap
 *   far arena  the block headers tile the arena exactly, with no two
 *              free blocks side by side after a free
 *
 * A seeded random workload then hammers both allocators, filling every
 * block it gets with a pattern and checking the pattern before freeing
 * it, so an allocator that handed out overlapping memory is caught.
 * Run under AddressSanitizer, a write past a heap block is caught too.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"

/*
 * mem_init (not called here) turns the linker's end-of-BSS symbol into
 * a 16-bit heap offset, which only makes sense on the target.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../../kernel/memory.c"
#pragma GCC diagnostic pop

uint8_t _kernel_end;
uint16_t hal_get_cs(void) { return KERNEL_CODE_SEG; }

/* mem_print_map (not called here) draws with the UI toolkit; these stand in for it */
void ui_panel_open(const char *title, uint8_t accent, const char *note) { (void)title; (void)accent; (void)note; }
void ui_panel_close(void) {}
void ui_row(void) {}
void ui_row_end(void) {}
void ui_row_rule(void) {}
void ui_text(uint8_t fg, const char *text) { (void)fg; (void)text; }
void ui_pad_to(uint8_t col) { (void)col; }
void ui_num(uint32_t value, uint8_t width, uint8_t fg) { (void)value; (void)width; (void)fg; }
void ui_hex(uint16_t value, uint8_t fg) { (void)value; (void)fg; }
void ui_meter(uint32_t used, uint32_t total, uint8_t width, uint8_t fg) { (void)used; (void)total; (void)width; (void)fg; }
char *ui_format(uint32_t value, const char *suffix, char *buf, uint8_t size) { (void)value; (void)suffix; (void)size; buf[0] = 0; return buf; }

#define HEAP_BYTES  8192
static unsigned char heap_area[HEAP_BYTES] __attribute__((aligned(16)));

static void heap_reset(void)
{
    heap_init(heap_area, HEAP_BYTES);
}

/* Walk the free list; false if an invariant is broken */
static bool heap_consistent(void)
{
    free_node_t *node;
    uint32_t free_bytes = 0;

    for (node = free_list; node != NULL; node = node->next) {
        unsigned char *start = (unsigned char *)node;

        if (start < heap_area || start + node->size > heap_area + HEAP_BYTES) return false;
        if (node->size < HEADER_SIZE) return false;
        if (node->next && (unsigned char *)node->next <= start + node->size) {
            return false;   /* out of order, overlapping, or touching but not merged */
        }
        free_bytes += node->size;
    }
    return free_bytes + total_allocated == total_heap_size;
}

static void test_heap_basics(void)
{
    void *a, *b, *c;
    uint16_t before;

    heap_reset();
    before = mem_free();
    a = kmalloc(100);
    b = kmalloc(200);
    c = kmalloc(50);
    CHECK(a && b && c && heap_consistent());
    CHECK(mem_free() < before);

    kfree(b);
    CHECK(heap_consistent());
    kfree(a);
    CHECK(heap_consistent());
    kfree(c);
    CHECK(heap_consistent() && mem_free() == before);
    CHECK(free_list->next == NULL);     /* merged back into one block */

    CHECK(kmalloc(0xFFF0) == NULL);     /* wraps in align_up; must fail cleanly */
    CHECK(kmalloc(HEAP_BYTES) == NULL);
    CHECK(heap_consistent());
    kfree(NULL);
    CHECK(heap_consistent());
}

static void test_heap_random(void)
{
    enum { SLOTS = 64, STEPS = 20000 };
    unsigned char *block[SLOTS] = { 0 };
    uint16_t size[SLOTS] = { 0 };
    unsigned step, broken = 0, corrupted = 0;

    heap_reset();
    srand(8086);
    for (step = 0; step < STEPS; step++) {
        int i = rand() % SLOTS;

        if (block[i]) {
            uint16_t k;
            for (k = 0; k < size[i]; k++) {
                if (block[i][k] != (unsigned char)i) corrupted++;
            }
            kfree(block[i]);
            block[i] = NULL;
        } else {
            size[i] = (uint16_t)(1 + rand() % 300);
            block[i] = kmalloc(size[i]);
            if (block[i]) memset(block[i], i, size[i]);
        }
        if (!heap_consistent()) broken++;
    }
    CHECK(broken == 0);
    CHECK(corrupted == 0);
}

/* ── Far arena ──────────────────────────────── */

/* Headers must tile [far_start, far_end) exactly */
static bool far_consistent(bool merged)
{
    uint16_t seg = far_start;
    bool previous_free = false;

    while (seg < far_end) {
        far_hdr_t *hdr = FAR_HDR(seg);

        if (hdr->magic != FAR_MAGIC) return false;
        if (merged && previous_free && !hdr->used) return false;
        previous_free = !hdr->used;
        seg += 1 + hdr->paras;
    }
    return seg == far_end;
}

static void test_far_basics(void)
{
    uint16_t before, a, b, c;
    uint8_t owner;

    memset(host_memory, 0, sizeof(host_memory));
    mem_kb = 640;
    far_init();
    CHECK(far_start == FAR_ARENA_SEG && far_end == 0xA000);
    before = far_free_paras();

    a = far_alloc(64);
    b = far_alloc_owned(128, 3);
    c = far_alloc_owned(16, 4);
    CHECK(a && b && c && far_consistent(false));
    CHECK(b >= a + 64 && c >= b + 128);

    CHECK(far_owner(a, &owner) && owner == FAR_NO_OWNER);
    CHECK(far_owner(b, &owner) && owner == 3);
    CHECK(!far_owner(b + 1, &owner));           /* not the start of a block */
    CHECK(!far_owner(0x1000, &owner));          /* not in the arena */

    far_free_owned(3);
    CHECK(!far_owner(b, &owner));
    far_free(a);
    far_free(c);
    CHECK(far_consistent(true) && far_free_paras() == before);

    CHECK(far_alloc(0) == 0 && far_alloc(0xFFFF) == 0);
    far_free(0x1234);                           /* ignored */
    CHECK(far_consistent(true));
}

static void test_far_random(void)
{
    enum { SLOTS = 32, STEPS = 5000 };
    uint16_t seg[SLOTS] = { 0 }, paras[SLOTS] = { 0 };
    unsigned step, broken = 0, corrupted = 0;
    uint16_t before;

    memset(host_memory, 0, sizeof(host_memory));
    far_init();
    before = far_free_paras();
    srand(4770);

    for (step = 0; step < STEPS; step++) {
        int i = rand() % SLOTS;

        if (seg[i]) {
            unsigned char *p = MK_FP(seg[i], 0);
            uint32_t k;
            for (k = 0; k < (uint32_t)paras[i] * 16; k++) {
                if (p[k] != (unsigned char)(i + 1)) corrupted++;
            }
            far_free(seg[i]);
            seg[i] = 0;
        } else {
            paras[i] = (uint16_t)(1 + rand() % 2000);
            seg[i] = far_alloc_owned(paras[i], (uint8_t)(i % 7));
            if (seg[i]) memset(MK_FP(seg[i], 0), i + 1, (size_t)paras[i] * 16);
        }
        if (!far_consistent(false)) broken++;
    }
    for (step = 0; step < 7; step++) {
        far_free_owned((int)step);
    }
    CHECK(broken == 0);
    CHECK(corrupted == 0);
    CHECK(far_consistent(true) && far_free_paras() == before);
}

static void test_far_small_machine(void)
{
    /* 192 KB: nothing above the kernel segments */
    far_start = far_end = 0;
    mem_kb = 192;
    far_init();
    CHECK(far_start == 0 && far_alloc(1) == 0 && far_free_paras() == 0);
    mem_kb = 640;
}

int main(void)
{
    host_reset();
    test_heap_basics();
    test_heap_random();
    test_far_basics();
    test_far_random();
    test_far_small_machine();
    return host_summary("memory");
}
