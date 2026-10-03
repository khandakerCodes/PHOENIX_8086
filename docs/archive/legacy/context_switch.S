/*
 * Phoenix-8086 — Context Switch (GAS / .code16gcc)
 *
 * Saves current thread's register state to old_tcb and
 * restores from new_tcb, performing the actual context switch.
 *
 * Uses .code16gcc for ABI compatibility with GCC -m16.
 *
 * void context_switch(tcb_t *old_tcb, tcb_t *new_tcb);
 *
 * TCB layout (offsets):
 *   +0x00: SP    +0x02: SS    +0x04: IP    +0x06: CS
 *   +0x08: FLAGS +0x0A: AX    +0x0C: BX    +0x0E: CX
 *   +0x10: DX    +0x12: SI    +0x14: DI    +0x16: BP
 */

    .code16gcc
    .section .text
    .globl context_switch

context_switch:
    /* Function prologue — ebp/esp are 32-bit in .code16gcc */
    pushl   %ebp
    movl    %esp, %ebp

    /* Get old_tcb pointer (first argument at 8(%ebp)) */
    movl    8(%ebp), %ebx

    /* Save general-purpose registers to old TCB */
    movw    %ax, 0x0A(%bx)
    movw    %cx, 0x0E(%bx)
    movw    %dx, 0x10(%bx)
    movw    %si, 0x12(%bx)
    movw    %di, 0x14(%bx)

    /* Save BP (original, before our prologue pushed it) */
    movl    %ebp, %eax
    movw    0(%eax), %ax            /* old ebp on stack */
    movw    %ax, 0x16(%bx)

    /* Save SP (before prologue) */
    movl    %ebp, %eax
    addl    $8, %eax                /* skip saved ebp + ret addr */
    movw    %ax, 0x00(%bx)

    /* Save SS */
    movw    %ss, 0x02(%bx)

    /* Save FLAGS */
    pushf
    popw    0x08(%bx)

    /* Get new_tcb pointer (second argument at 12(%ebp)) */
    movl    12(%ebp), %ebx

    /* Restore general-purpose registers from new TCB */
    movw    0x0A(%bx), %ax
    movw    0x0E(%bx), %cx
    movw    0x10(%bx), %dx
    movw    0x12(%bx), %si
    movw    0x14(%bx), %di
    movw    0x16(%bx), %bp

    /* Restore FLAGS */
    pushw   0x08(%bx)
    popf

    /* Restore stack */
    movw    0x02(%bx), %ss
    movw    0x00(%bx), %sp

    /* Push return address (CS:IP from new TCB) for retf */
    pushw   0x06(%bx)               /* CS */
    pushw   0x04(%bx)               /* IP */
    lret                            /* pop IP, pop CS → resume */
