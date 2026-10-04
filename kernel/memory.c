/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Memory Manager Implementation
 *
 * Simple first-fit near-heap allocator inside the kernel data
 * segment, between the end of the kernel's BSS and the kernel
 * stack. Uses a linked free list where each free block contains
 * a size and a next pointer.
 *
 * This is a basic allocator suitable for a 16-bit real-mode
 * kernel with limited memory.
 */

#include "memory.h"
#include "console.h"
#include "kernel.h"
#include "hal.h"
#include "ui.h"

/* ── Free list node ─────────────────────────── */
typedef struct free_node {
    uint16_t size;              /* Size of this free block (including header) */
    struct free_node *next;     /* Next free block */
} free_node_t;

/* Minimum allocation alignment */
#define ALIGN       4
#define HEADER_SIZE sizeof(free_node_t)

/* ── Heap state ─────────────────────────────── */
static free_node_t *free_list = NULL;
static uint16_t total_heap_size = 0;
static uint16_t total_allocated = 0;
static uint16_t heap_start = 0;
static uint16_t heap_end = 0;

/* ── Internal helpers ───────────────────────── */

static uint16_t align_up(uint16_t val, uint16_t align)
{
    return (val + align - 1) & ~(align - 1);
}

/* ── Public API ─────────────────────────────── */

/* ── Far arena ──────────────────────────────── */

/*
 * Every far block is preceded by a one-paragraph header, so a block
 * returned as segment S has its header at (S-1):0000. Blocks are
 * laid end to end; walking the headers visits the whole arena.
 */
typedef struct {
    uint16_t magic;     /* FAR_MAGIC */
    uint16_t paras;     /* Size of the data area in paragraphs */
    uint8_t  used;
    uint8_t  owner;     /* Thread that allocated it, or FAR_NO_OWNER */
} far_hdr_t;

#define FAR_MAGIC       0x4D46  /* "FM" */
#define FAR_HDR(seg)    ((far_hdr_t __far *)MK_FP(seg, 0))

/* Arena bounds as segments; both 0 if there is no memory above the kernel */
static uint16_t far_start = 0;
static uint16_t far_end = 0;

static void far_init(void)
{
    /* INT 12h reports KB of conventional memory; 1 KB = 64 paragraphs */
    uint16_t top = (mem_kb > 640) ? 0xA000 : (uint16_t)(mem_kb * 64);
    far_hdr_t __far *hdr;

    if (top <= FAR_ARENA_SEG + 1) {
        return;  /* Machine has no memory above the kernel segments */
    }

    far_start = FAR_ARENA_SEG;
    far_end   = top;

    hdr = FAR_HDR(far_start);
    hdr->magic = FAR_MAGIC;
    hdr->paras = far_end - far_start - 1;
    hdr->used  = false;
}

/*
 * Merge every run of adjacent free blocks into one. Each merge also
 * reclaims the header paragraph of the absorbed block. Interrupts
 * must be off.
 */
static void far_merge_free(void)
{
    uint16_t seg;

    for (seg = far_start; seg < far_end; seg += 1 + FAR_HDR(seg)->paras) {
        far_hdr_t __far *hdr = FAR_HDR(seg);
        uint16_t next;

        if (hdr->magic != FAR_MAGIC) break;  /* Arena corrupted */
        if (hdr->used) continue;

        next = seg + 1 + hdr->paras;
        while (next < far_end && FAR_HDR(next)->magic == FAR_MAGIC &&
               !FAR_HDR(next)->used) {
            hdr->paras += 1 + FAR_HDR(next)->paras;
            next = seg + 1 + hdr->paras;
        }
    }
}

uint16_t far_alloc(uint16_t paragraphs)
{
    return far_alloc_owned(paragraphs, FAR_NO_OWNER);
}

uint16_t far_alloc_owned(uint16_t paragraphs, uint8_t owner)
{
    uint16_t flags;
    uint16_t seg;
    uint16_t result = 0;

    if (paragraphs == 0 || far_start == 0) return 0;

    flags = hal_irq_save();

    /* First-fit walk over the block headers */
    for (seg = far_start; seg < far_end; seg += 1 + FAR_HDR(seg)->paras) {
        far_hdr_t __far *hdr = FAR_HDR(seg);

        if (hdr->magic != FAR_MAGIC) break;  /* Arena corrupted */
        if (hdr->used || hdr->paras < paragraphs) continue;

        /* Split off the remainder if it can hold a header and data */
        if (hdr->paras > paragraphs + 1) {
            far_hdr_t __far *rest = FAR_HDR(seg + 1 + paragraphs);
            rest->magic = FAR_MAGIC;
            rest->paras = hdr->paras - paragraphs - 1;
            rest->used  = false;
            hdr->paras  = paragraphs;
        }

        hdr->used = true;
        hdr->owner = owner;
        result = seg + 1;
        break;
    }

    hal_irq_restore(flags);
    return result;
}

void far_free(uint16_t segment)
{
    uint16_t flags;
    far_hdr_t __far *hdr;

    if (segment <= far_start || segment >= far_end) return;

    flags = hal_irq_save();
    hdr = FAR_HDR(segment - 1);
    if (hdr->magic == FAR_MAGIC && hdr->used) {
        hdr->used = false;
        far_merge_free();
    }
    hal_irq_restore(flags);
}

bool far_owner(uint16_t segment, uint8_t *owner)
{
    uint16_t flags;
    far_hdr_t __far *hdr;
    bool found = false;

    if (segment <= far_start || segment >= far_end) return false;

    flags = hal_irq_save();
    hdr = FAR_HDR(segment - 1);
    if (hdr->magic == FAR_MAGIC && hdr->used) {
        *owner = hdr->owner;
        found = true;
    }
    hal_irq_restore(flags);
    return found;
}

void far_free_owned(int tid)
{
    uint16_t flags;
    uint16_t seg;
    bool freed = false;

    if (far_start == 0) return;

    flags = hal_irq_save();
    for (seg = far_start; seg < far_end; seg += 1 + FAR_HDR(seg)->paras) {
        far_hdr_t __far *hdr = FAR_HDR(seg);

        if (hdr->magic != FAR_MAGIC) break;
        if (hdr->used && hdr->owner == (uint8_t)tid) {
            hdr->used = false;
            freed = true;
        }
    }
    if (freed) {
        far_merge_free();
    }
    hal_irq_restore(flags);
}

uint16_t far_free_paras(void)
{
    uint16_t flags;
    uint16_t seg;
    uint16_t total = 0;

    if (far_start == 0) return 0;

    flags = hal_irq_save();
    for (seg = far_start; seg < far_end; seg += 1 + FAR_HDR(seg)->paras) {
        if (FAR_HDR(seg)->magic != FAR_MAGIC) break;
        if (!FAR_HDR(seg)->used) {
            total += FAR_HDR(seg)->paras;
        }
    }
    hal_irq_restore(flags);
    return total;
}

/* Make `size` bytes at `start` the whole near heap: one free block */
static void heap_init(void *start, uint16_t size)
{
    total_heap_size = size;
    total_allocated = 0;

    free_list = (free_node_t *)start;
    free_list->size = size;
    free_list->next = NULL;
}

/* ── Public API ─────────────────────────────── */

void mem_init(void)
{
    /*
     * The near heap covers the rest of the kernel data segment:
     * from the end of BSS up to the bottom of the kernel stack.
     */
    extern uint8_t _kernel_end;

    heap_start = align_up((uint16_t)&_kernel_end, ALIGN);
    heap_end   = KERNEL_STACK_TOP + 2 - KERNEL_STACK_SIZE;
    heap_init((void *)heap_start, heap_end - heap_start);

    far_init();
}

void *kmalloc(uint16_t size)
{
    uint16_t flags = hal_irq_save();
    free_node_t *prev = NULL;
    free_node_t *curr = free_list;
    uint16_t needed = align_up(size + HEADER_SIZE, ALIGN);
    void *result = NULL;

    /* A request this large wrapped around in align_up */
    if (needed < size) {
        curr = NULL;
    }

    /* First-fit search */
    while (curr != NULL) {
        if (curr->size >= needed) {
            /* Found a suitable block */

            if (curr->size >= needed + HEADER_SIZE + ALIGN) {
                /* Split the block */
                free_node_t *new_free = (free_node_t *)((uint8_t *)curr + needed);
                new_free->size = curr->size - needed;
                new_free->next = curr->next;

                if (prev) {
                    prev->next = new_free;
                } else {
                    free_list = new_free;
                }

                curr->size = needed;
            } else {
                /* Use the entire block */
                if (prev) {
                    prev->next = curr->next;
                } else {
                    free_list = curr->next;
                }
            }

            total_allocated += curr->size;

            /* Return pointer past the header */
            result = (void *)((uint8_t *)curr + HEADER_SIZE);
            break;
        }

        prev = curr;
        curr = curr->next;
    }

    hal_irq_restore(flags);
    return result;
}

void kfree(void *ptr)
{
    uint16_t flags;
    free_node_t *block;
    free_node_t *prev;
    free_node_t *curr;

    if (ptr == NULL) return;

    flags = hal_irq_save();

    /* Get the block header (it's just before the user data) */
    block = (free_node_t *)((uint8_t *)ptr - HEADER_SIZE);

    total_allocated -= block->size;

    /* Insert into free list in address order (for coalescing) */
    prev = NULL;
    curr = free_list;

    while (curr != NULL && curr < block) {
        prev = curr;
        curr = curr->next;
    }

    /* Insert block between prev and curr */
    block->next = curr;
    if (prev) {
        prev->next = block;
    } else {
        free_list = block;
    }

    /* Coalesce with next block if adjacent */
    if (block->next != NULL &&
        (uint8_t *)block + block->size == (uint8_t *)block->next) {
        block->size += block->next->size;
        block->next = block->next->next;
    }

    /* Coalesce with previous block if adjacent */
    if (prev != NULL &&
        (uint8_t *)prev + prev->size == (uint8_t *)block) {
        prev->size += block->size;
        prev->next = block->next;
    }

    hal_irq_restore(flags);
}

uint16_t mem_free(void)
{
    uint16_t flags = hal_irq_save();
    uint16_t total = 0;
    free_node_t *curr = free_list;
    while (curr != NULL) {
        total += curr->size;
        curr = curr->next;
    }
    hal_irq_restore(flags);
    return total;
}

uint16_t mem_used(void)
{
    return total_allocated;
}

void mem_get_layout(mem_layout_t *layout)
{
    layout->heap_start = heap_start;
    layout->heap_end   = heap_end;
    layout->far_start  = far_start;
    layout->far_end    = far_end;
}

/* Print "ssss:oooo" */
/* Map table columns */
#define MAP_ADDRESS     19
#define MAP_NOTE        52

static void print_seg_off(uint16_t seg, uint16_t off)
{
    ui_hex(seg, TH_TEXT);
    ui_text(TH_OVERLAY, ":");
    ui_hex(off, TH_TEXT);
}

/* One region: name, segment:offset start, the last offset or segment, and a note */
static void region(const char *name, uint16_t seg, uint16_t off, const char *dash, uint16_t last,
                   bool last_is_segment, const char *note)
{
    ui_row();
    ui_text(TH_SUBTEXT, name);
    ui_pad_to(MAP_ADDRESS);
    print_seg_off(seg, off);
    if (dash) {
        ui_text(TH_OVERLAY, dash);
        if (last_is_segment) {
            print_seg_off(last, 0);
        } else {
            ui_hex(last, TH_TEXT);
        }
    }
    if (note) {
        ui_pad_to(MAP_NOTE);
        ui_text(TH_OVERLAY, note);
    }
    ui_row_end();
}

/* "Label:  N unit" and a meter of how much is in use */
static void usage(const char *label, uint32_t value, const char *unit, uint32_t used, uint32_t total)
{
    uint8_t share = total ? (uint8_t)(used * 100 / total) : 0;

    ui_row();
    ui_text(TH_SUBTEXT, label);
    ui_pad_to(15);
    ui_num(value, 6, TH_TEXT);
    con_putchar(' ');
    ui_text(TH_SUBTEXT, unit);
    if (total) {
        ui_pad_to(MAP_ADDRESS + 10);
        ui_meter(used, total, 30, share > 85 ? TH_RED : share > 60 ? TH_YELLOW : TH_TEAL);
        ui_num(share, 4, TH_TEXT);
        ui_text(TH_SUBTEXT, "% used");
    }
    ui_row_end();
}

void mem_print_map(void)
{
    char note[24];
    uint16_t heap_free = mem_free(), heap_used = mem_used();
    uint16_t far_total = far_end - far_start, far_left = far_free_paras();

    ui_panel_open("Memory Map", TH_TEAL, ui_format(mem_kb, " KB of memory", note, sizeof(note)));
    region("IVT", 0x0000, 0x0000, " - ", 0x03FF, false, "interrupt vectors");
    region("BIOS Data", 0x0000, 0x0400, " - ", 0x04FF, false, NULL);
    region("Bootloader", 0x0000, 0x7C00, NULL, 0, false, "stage 1, 512 bytes");
    region("Stage 2", 0x0000, 0x7E00, NULL, 0, false, "2048 bytes");
    region("Kernel Code", hal_get_cs(), 0, NULL, 0, false, "64 KB segment");
    region("Kernel Data", KERNEL_DATA_SEG, 0, " - ", heap_start - 1, false, "data and BSS");
    region("Near Heap", KERNEL_DATA_SEG, heap_start, " - ", heap_end - 1, false, "kmalloc");
    region("Kernel Stack", KERNEL_DATA_SEG, heap_end, " - ", KERNEL_STACK_TOP + 1, false, NULL);
    region("Far Arena", far_start, 0, " - ", far_end, true, "programs, far_alloc");
    ui_row_rule();
    usage("Heap free:", heap_free, "bytes", heap_used, (uint32_t)heap_free + heap_used);
    usage("Heap used:", heap_used, "bytes", 0, 0);
    usage("Far free:", far_left / 64, "KB", far_total - far_left, far_total);
    ui_panel_close();
}
