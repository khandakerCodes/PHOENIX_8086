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

/* Current code segment */
uint16_t hal_get_cs(void);

/* Build a far pointer from segment:offset */
#define MK_FP(seg, off) \
    ((void __far *)(((uint32_t)(uint16_t)(seg) << 16) | (uint16_t)(off)))

#endif /* PHOENIX_HAL_H */
