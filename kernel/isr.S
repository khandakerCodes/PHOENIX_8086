/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — ISR Stubs and Context Switch (GAS, 8086 only)
 *
 * Every stub saves the full register context (frame_t in tcb.h),
 * makes sure it is running on the current thread's kernel stack,
 * passes the saved context to its C handler, and resumes whatever
 * context the handler returns:
 *
 *     uint16_t handler(uint16_t context);
 *
 * Returning the same context resumes the interrupted thread. Returning
 * another thread's saved context is the context switch.
 *
 * Two cases on entry (see context_t in tcb.h):
 *
 *   Interrupted kernel code. SS is already the kernel data segment and
 *   the frame was pushed on the thread's kernel stack. Nothing to move.
 *
 *   Interrupted a program. SS is the program's own data segment. C
 *   code cannot run there (the compiler assumes SS = DS), so the stub
 *   switches to the thread's kernel stack, copies the frame onto it,
 *   and records where the program's stack was. On the way out it
 *   copies the frame back and returns on the program's stack.
 *
 * The 8086 has no PUSHA/POPA, so registers are pushed one by one.
 */

    .arch i8086,jumps
    .code16
    .section .text

    .globl kernel_ds

#define FRAME_SIZE  24      /* sizeof(frame_t) */
#define FRAME_WORDS 12

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

    movw    %ss, %bx
    cmpw    %ax, %bx
    je      1f                      /* Already on a kernel stack */

    /* Came from a program: BX:SP is the frame on the program's stack */
    movw    %sp, %si
    movw    %ax, %ss
    movw    kernel_stack_top, %sp   /* This thread's kernel stack, empty */
    pushw   %bx                     /* user_ss */
    pushw   %si                     /* user_sp */
    subw    $FRAME_SIZE, %sp
    movw    %sp, %di                /* ES:DI = frame on the kernel stack */
    movw    %bx, %ds                /* DS:SI = frame on the program's stack */
    movw    $FRAME_WORDS, %cx
    rep     movsw
    movw    %ax, %ds
    movw    $1, %bx
    jmp     2f
1:
    xorw    %bx, %bx
2:
    pushw   %bx                     /* from_program */

    movw    %sp, %ax                /* SP now points at the context */
    pushw   %ax
    call    \handler
    jmp     isr_resume              /* AX = the context to resume */
.endm

/*
 * Resume the context in AX: the second half of every stub.
 */
isr_resume:
    movw    %ax, %sp
    cld
    popw    %ax                     /* from_program */
    testw   %ax, %ax
    jz      3f

    /* Back to a program: put the frame on its stack and switch to it */
    movw    %sp, %bp
    movw    %sp, %si                /* DS:SI = frame on the kernel stack */
    movw    FRAME_SIZE(%bp), %di    /* user_sp */
    movw    FRAME_SIZE+2(%bp), %es  /* user_ss; ES:DI = frame on the program's stack */
    movw    $FRAME_WORDS, %cx
    rep     movsw
    movw    FRAME_SIZE(%bp), %cx
    movw    FRAME_SIZE+2(%bp), %bx
    movw    %bx, %ss
    movw    %cx, %sp
3:
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
