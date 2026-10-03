/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Memory Layout
 *
 * Shared by C and assembly (preprocessor defines only).
 * boot/stage2.asm carries its own copy of the load segment
 * and header offsets — keep them in sync.
 *
 * Physical memory map:
 *   0x00000 - 0x003FF  Interrupt Vector Table
 *   0x00400 - 0x004FF  BIOS Data Area
 *   0x00500 - 0x07BFF  Boot-time stack (free after kernel start)
 *   0x07C00 - 0x07DFF  Stage 1 Bootloader
 *   0x07E00 - 0x0FFFF  Stage 2 Loader
 *   0x10000 - 0x1FFFF  Kernel code segment  (CS = 0x1000)
 *   0x20000 - 0x2FFFF  Kernel data segment  (DS = SS = 0x2000)
 *   0x30000 - top      Far arena (far_alloc; top from BIOS INT 12h)
 */

#ifndef PHOENIX_LAYOUT_H
#define PHOENIX_LAYOUT_H

/* Segments */
#define KERNEL_CODE_SEG     0x1000
#define KERNEL_DATA_SEG     0x2000

/* Far arena: first segment after the kernel data segment */
#define FAR_ARENA_SEG        0x3000

/* Kernel stack: top of the data segment, grows down */
#define KERNEL_STACK_TOP    0xFFFE
#define KERNEL_STACK_SIZE   0x0800

/* Offset 0 of the data segment is reserved so no object has address NULL */
#define KERNEL_DATA_RESERVED 16

/*
 * Kernel image header (first 16 bytes of kernel.bin, in the code segment)
 *   +0  magic "PX86"
 *   +4  header version
 *   +6  image size in bytes (text + data)
 *   +8  text size
 *   +10 data size
 *   +12 bss size
 *   +14 checksum (16-bit sum of all image bytes with this field zero)
 * Execution starts at KERNEL_ENTRY_OFF.
 */
#define KERNEL_HDR_VERSION  1
#define KERNEL_HDR_SIZE     16
#define KERNEL_ENTRY_OFF    0x0010

#endif /* PHOENIX_LAYOUT_H */
