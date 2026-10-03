/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Kernel Entry Point (GAS, 8086 only)
 *
 * This is the first kernel code executed after Stage 2 hands off.
 * Stage 2 loads the whole image at KERNEL_CODE_SEG:0000 and far-jumps
 * to KERNEL_ENTRY_OFF with DL = boot drive, CX = memory in KB.
 *
 * The image starts with a 16-byte header (see include/layout.h).
 */

#include "layout.h"

    .arch i8086,jumps
    .code16
    .section .text
    .globl kernel_entry
    .globl halt
    .globl boot_drive
    .globl mem_kb

/*
 * Image header — must be at offset 0 of the code segment
 */
kernel_header:
    .ascii  "PX86"
    .word   KERNEL_HDR_VERSION
    .word   __image_size
    .word   __text_size
    .word   __data_size
    .word   __bss_size
    .word   0                       /* checksum, patched by tools/mkimage.py */

/*
 * kernel_entry — offset KERNEL_ENTRY_OFF
 */
kernel_entry:
    cli
    cld

    movw    %cx, %bp                /* keep memory size; DL stays untouched */

    /* Copy the data image (stored right after .text) into the data segment */
    movw    %cs, %ax
    addw    $__text_paras, %ax
    movw    %ax, %ds
    xorw    %si, %si
    movw    $KERNEL_DATA_SEG, %ax
    movw    %ax, %es
    xorw    %di, %di
    movw    $__data_size, %cx
    rep     movsb

    /* Zero the BSS section */
    movw    $__bss_start, %di
    movw    $__bss_size, %cx
    xorb    %al, %al
    rep     stosb

    /* Set up segments and stack — DS = ES = SS = kernel data segment */
    movw    $KERNEL_DATA_SEG, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $KERNEL_STACK_TOP, %sp

    /* ISR stubs reload DS from this word */
    movw    %ax, %cs:kernel_ds

    /* Save boot info passed from Stage 2 */
    movb    %dl, boot_drive
    movw    %bp, mem_kb

    sti

    call    kernel_main

    /* If kernel_main returns, halt */
halt:
    cli
    hlt
    jmp     halt

/*
 * Boot data — accessible from C as extern globals
 */
    .section .data
boot_drive:
    .byte   0
    .balign 2
mem_kb:
    .word   0
