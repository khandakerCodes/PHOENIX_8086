/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — ISR Stubs and Context Switch (GAS, 8086 only)
 *
 * Every stub saves the full register context on the current
 * thread's stack (the layout is frame_t in tcb.h), passes the
 * resulting stack pointer to its C handler, and resumes whatever
 * stack pointer the handler returns:
 *
 *     uint16_t handler(uint16_t sp);
 *
 * Returning the same SP resumes the interrupted thread. Returning
 * another thread's saved SP is the context switch: the pops and
 * the IRET below then restore that thread instead. All stacks live
 * in the kernel data segment, so SS never changes.
 *
 * The 8086 has no PUSHA/POPA, so registers are pushed one by one.
 * The interrupted code may have DS pointing anywhere (far pointer
 * access), so DS/ES are reloaded from kernel_ds before calling C.
 */

    .arch i8086,jumps
    .code16
    .section .text

    .globl kernel_ds

/* Kernel data segment, written once by entry.S */
kernel_ds:
    .word   0

.macro ISR_STUB name, handler
    .globl  \name
\name:
    pushw   %ax
    pushw   %cx
    pushw   %dx
    pushw   %bx
    pushw   %bp
    pushw   %si
    pushw   %di
    pushw   %ds
    pushw   %es

    movw    %cs:kernel_ds, %ax
    movw    %ax, %ds
    movw    %ax, %es
    cld

    movw    %sp, %ax                /* SP now points at the frame */
    pushw   %ax
    call    \handler
    movw    %ax, %sp                /* Resume the frame the handler chose */

    popw    %es
    popw    %ds
    popw    %di
    popw    %si
    popw    %bp
    popw    %bx
    popw    %dx
    popw    %cx
    popw    %ax
    iret
.endm

/* Timer ISR (IRQ0 — Vector 0x08) */
ISR_STUB timer_isr, timer_handler

/* Keyboard ISR (IRQ1 — Vector 0x09) */
ISR_STUB keyboard_isr, keyboard_handler

/* System Call ISR (INT 80h) */
ISR_STUB syscall_isr, syscall_handler

/* Voluntary yield (INT 81h) */
ISR_STUB yield_isr, yield_handler

/* Kernel panic (INT 82h) and CPU exception traps — handlers never return */
ISR_STUB panic_isr, panic_handler
ISR_STUB exc_divide_isr, exc_divide_handler
ISR_STUB exc_step_isr, exc_step_handler
ISR_STUB exc_break_isr, exc_break_handler
ISR_STUB exc_overflow_isr, exc_overflow_handler

/* Floppy controller (IRQ6 — Vector 0x0E) */
ISR_STUB floppy_isr, floppy_handler
