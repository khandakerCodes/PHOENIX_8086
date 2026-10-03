/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK — Program Startup and System Call Stub
 *
 * The loader starts a program at _start as a new thread. DS, ES and
 * SS already point at the program's own data segment, and SP at a
 * stack inside it.
 */

    .arch i8086,jumps
    .code16
    .section .text

    .globl _start
    .globl px_syscall
    .globl px_syscall32
    .globl px_thread_entry

_start:
    call    main

    /* thread_exit — main returned */
    movb    $0x05, %ah
    int     $0x80
1:
    jmp     1b

/*
 * px_thread_entry — where a thread started with px_thread_create
 * begins. The kernel passes the function to run in SI; when it
 * returns, the thread exits.
 */
px_thread_entry:
    call    *%si
    movb    $0x05, %ah
    int     $0x80
2:
    jmp     2b

/*
 * unsigned      px_syscall(unsigned ax, unsigned bx, unsigned cx, unsigned dx)
 * unsigned long px_syscall32(unsigned ax, unsigned bx, unsigned cx, unsigned dx)
 *
 * Issue INT 80h. The same code under two names: the result is in AX
 * for the first and in DX:AX for the second.
 */
px_syscall:
px_syscall32:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %ax
    movw    6(%bp), %bx
    movw    8(%bp), %cx
    movw    10(%bp), %dx
    int     $0x80
    popw    %bp
    ret
