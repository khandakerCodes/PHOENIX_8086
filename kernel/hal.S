/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Hardware Access Helpers (GAS, 8086 only)
 *
 * Port I/O and segment register access for C code.
 * cdecl: arguments on the stack, result in AX.
 */

    .arch i8086,jumps
    .code16
    .section .text

    .globl outb
    .globl inb
    .globl hal_get_cs

/* void outb(uint16_t port, uint8_t val) */
outb:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %dx
    movb    6(%bp), %al
    outb    %al, %dx
    popw    %bp
    ret

/* uint8_t inb(uint16_t port) */
inb:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %dx
    inb     %dx, %al
    xorb    %ah, %ah
    popw    %bp
    ret

/* uint16_t hal_get_cs(void) */
hal_get_cs:
    movw    %cs, %ax
    ret

    .globl hal_irq_save
    .globl hal_irq_restore
    .globl sys_call

/* uint16_t hal_irq_save(void) — return FLAGS, then disable interrupts */
hal_irq_save:
    pushf
    popw    %ax
    cli
    ret

/* void hal_irq_restore(uint16_t flags) — restore FLAGS from hal_irq_save */
hal_irq_restore:
    pushw   %bp
    movw    %sp, %bp
    pushw   4(%bp)
    popf
    popw    %bp
    ret

/*
 * uint32_t sys_call(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx)
 * Issue INT 80h with the given registers; result in DX:AX.
 */
sys_call:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %ax
    movw    6(%bp), %bx
    movw    8(%bp), %cx
    movw    10(%bp), %dx
    int     $0x80
    popw    %bp
    ret
