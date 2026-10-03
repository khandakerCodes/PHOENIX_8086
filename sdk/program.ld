/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK — Program Linker Script
 *
 * A program has two parts, each loaded into a segment of its own:
 *
 *   .text          the code segment, starting at offset 0
 *   .data + .bss   the data segment, starting at offset 16. The first
 *                  16 bytes are left unused so that no object has
 *                  address 0 and a null pointer is never valid. The
 *                  loader puts the thread stacks after the bss.
 */

OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    .text 0 : {
        *crt0.o(.text)
        *(.text)
        *(.text.*)
    } =0x90909090

    .data 16 : {
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        . = ALIGN(2);
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(2);
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
