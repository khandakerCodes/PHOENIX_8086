/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Kernel Linker Script (ia16-elf, small model)
 *
 * Code and data live in separate 64 KB segments, so both start at
 * offset 0 of their own segment:
 *
 *   .text          CS = 0x1000   loaded at file offset 0
 *   .data + .bss   DS = 0x2000   stored after .text in the file;
 *                                copied into place by kernel/entry.S
 *
 * The link produces kernel.elf (for symbols and the opcode check);
 * objcopy flattens it to kernel.bin by load address.
 * See include/layout.h for the full memory map.
 */

OUTPUT_ARCH(i386)
ENTRY(kernel_entry)

SECTIONS
{
    /* ── Code segment ─────────────────────────── */
    .text 0 : AT(0) {
        /* entry.o first: the image header must be at offset 0 */
        *entry.o(.text)
        *(.text)
        *(.text.*)
        . = ALIGN(16);
    } =0x90909090       /* pad with one-byte NOPs (the default fill is a 386 NOP) */
    __text_size  = SIZEOF(.text);
    __text_paras = SIZEOF(.text) / 16;

    /* ── Data segment ─────────────────────────── */
    .data 0 : AT(__text_size) {
        /* Reserve offset 0 so no object has address NULL */
        . = . + 16;
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        . = ALIGN(2);
    }
    __data_size = SIZEOF(.data);

    /* BSS — zeroed at runtime by kernel entry code */
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(2);
        __bss_end = .;
    }
    __bss_size = SIZEOF(.bss);

    /* End of kernel data — the heap starts here */
    _kernel_end = .;

    __image_size = __text_size + __data_size;

    /* The loader and entry code assume the image fits in one segment */
    ASSERT(__image_size <= 0xFE00, "kernel image exceeds 64 KB code segment")
    /* Leave at least 8 KB of the data segment for heap and kernel stack */
    ASSERT(_kernel_end <= 0xE000, "kernel data + bss leave no room for heap and stack")

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
