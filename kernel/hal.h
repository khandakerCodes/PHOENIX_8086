/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Hardware Access Helpers Header
 */

#ifndef PHOENIX_HAL_H
#define PHOENIX_HAL_H

#include "../include/types.h"

/* Port I/O (implemented in hal.S) */
void    outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);

/*
 * Interrupt-safe critical sections (implemented in hal.S).
 * Safe to nest and safe inside interrupt handlers:
 *
 *     uint16_t flags = hal_irq_save();
 *     ...
 *     hal_irq_restore(flags);
 */
uint16_t hal_irq_save(void);
void     hal_irq_restore(uint16_t flags);

/* Issue INT 80h from kernel-mode test code; result in DX:AX */
uint32_t sys_call(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx);

/* BIOS disk services (hal.S); see disk.c for the conditions */
uint16_t bios_disk_read(uint16_t drive, uint16_t cylinder, uint16_t head,
                        uint16_t sector, void *buffer);
void     bios_disk_reset(uint16_t drive);

/* Processor family, detected at run time (see hal.S) */
#define CPU_8086        0   /* 8086 or 8088 */
#define CPU_80186       1   /* 80186/80188 or NEC V20/V30 */
#define CPU_80286_PLUS  2   /* 80286 or later */

uint16_t hal_cpu_class(void);

/* Current code segment */
uint16_t hal_get_cs(void);

/* Store `value` in `count` words starting at segment:offset */
void hal_fill_words(uint16_t segment, uint16_t offset, uint16_t count, uint16_t value);

/* How many of `count` words at segment:offset equal `value` before the first that does not */
uint16_t hal_count_words(uint16_t segment, uint16_t offset, uint16_t count, uint16_t value);

/* Build a far pointer from segment:offset */
#define MK_FP(seg, off) \
    ((void __far *)(((uint32_t)(uint16_t)(seg) << 16) | (uint16_t)(off)))

#endif /* PHOENIX_HAL_H */
