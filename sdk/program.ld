/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK — Program Linker Script
 *
 * A program has two parts, each linked at offset 0:
 *
 *   .text          runs in its own code segment (far memory)
 *   .data + .bss   live in the kernel's data segment, wherever the
 *                  loader finds room; the loader adds that address to
 *                  every data reference listed by sdk/mkprog.py
 *
 * Link with "-q" so the relocations stay in the output for mkprog.py.
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

    .data 0 : {
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
