/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK — Program Startup and System Call Stub
 *
 * The loader starts a program at _start as a new thread. DS, ES and
 * SS already point at the kernel data segment, where the program's
 * own data was placed, and the stack is the thread's stack.
 */

    .arch i8086,jumps
    .code16
    .section .text

    .globl _start
    .globl px_syscall

_start:
    call    main

    /* thread_exit — main returned */
    movb    $0x05, %ah
    int     $0x80
1:
    jmp     1b

/*
 * unsigned long px_syscall(unsigned ax, unsigned bx, unsigned cx, unsigned dx)
 * Issue INT 80h; the result comes back in DX:AX.
 */
px_syscall:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %ax
    movw    6(%bp), %bx
    movw    8(%bp), %cx
    movw    10(%bp), %dx
    int     $0x80
    popw    %bp
    ret
