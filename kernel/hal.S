/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Hardware Access Helpers (GAS, 8086 only)
 *
 * Port I/O and segment register access for C code.
 * cdecl: arguments on the stack, result in AX.
 */

    .arch i8086,jumps
    .code16
    .section .text

    .globl outb
    .globl inb
    .globl hal_get_cs

/* void outb(uint16_t port, uint8_t val) */
outb:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %dx
    movb    6(%bp), %al
    outb    %al, %dx
    popw    %bp
    ret

/* uint8_t inb(uint16_t port) */
inb:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %dx
    inb     %dx, %al
    xorb    %ah, %ah
    popw    %bp
    ret

/* uint16_t hal_get_cs(void) */
hal_get_cs:
    movw    %cs, %ax
    ret

    .globl hal_irq_save
    .globl hal_irq_restore
    .globl sys_call

/* uint16_t hal_irq_save(void) — return FLAGS, then disable interrupts */
hal_irq_save:
    pushf
    popw    %ax
    cli
    ret

/* void hal_irq_restore(uint16_t flags) — restore FLAGS from hal_irq_save */
hal_irq_restore:
    pushw   %bp
    movw    %sp, %bp
    pushw   4(%bp)
    popf
    popw    %bp
    ret

/*
 * uint32_t sys_call(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx)
 * Issue INT 80h with the given registers; result in DX:AX.
 */
sys_call:
    pushw   %bp
    movw    %sp, %bp
    movw    4(%bp), %ax
    movw    6(%bp), %bx
    movw    8(%bp), %cx
    movw    10(%bp), %dx
    int     $0x80
    popw    %bp
    ret

    .globl hal_cpu_class

/*
 * uint16_t hal_cpu_class(void)
 * Identify the processor family using only 8086 instructions:
 *   0 = 8086/8088, 1 = 80186/80188 or NEC V20/V30, 2 = 80286 or later.
 *
 * On the 8086 and 186, FLAGS bits 12-15 always read as 1. The 8086
 * shifts by the full count in CL; the 186 and later use only the low
 * five bits, so a shift by 33 behaves as a shift by 1.
 */
hal_cpu_class:
    pushf                           /* keep the caller's flags */
    cli
    xorw    %ax, %ax
    pushw   %ax
    popf                            /* try to clear every flag */
    pushf
    popw    %ax
    andw    $0xF000, %ax
    cmpw    $0xF000, %ax
    movw    $2, %ax
    jne     1f                      /* bits 12-15 cleared: 286 or later */

    movw    $1, %ax
    movb    $33, %cl
    shlw    %cl, %ax                /* 8086: 0, 186: 2 */
    testw   %ax, %ax
    movw    $1, %ax
    jnz     1f
    xorw    %ax, %ax
1:
    popf
    ret

    .globl bios_disk_read
    .globl bios_disk_reset

/*
 * uint16_t bios_disk_read(uint16_t drive, uint16_t cylinder, uint16_t head,
 *                         uint16_t sector, void *buffer)
 * Read one sector into DS:buffer with BIOS INT 13h. Returns 0 on
 * success, otherwise 0x100 | BIOS status. The caller must have put the
 * BIOS back in charge of the timer first (irq_bios_enter); interrupts
 * are on during the call and off again on return.
 */
bios_disk_read:
    pushw   %bp
    movw    %sp, %bp
    pushw   %si
    pushw   %di
    pushw   %es
    pushw   %ds

    movb    4(%bp), %dl             /* drive */
    movb    6(%bp), %ch             /* cylinder */
    movb    8(%bp), %dh             /* head */
    movb    10(%bp), %cl            /* sector (1-based) */
    movw    12(%bp), %bx            /* ES:BX = buffer */
    pushw   %ds
    popw    %es
    movw    $0x0201, %ax            /* AH=02 read, AL=1 sector */
    sti
    int     $0x13
    cli
    cld

    popw    %ds
    jc      1f
    xorw    %ax, %ax
    jmp     2f
1:
    movb    %ah, %al
    movb    $1, %ah
2:
    popw    %es
    popw    %di
    popw    %si
    popw    %bp
    ret

/* void bios_disk_reset(uint16_t drive) — BIOS INT 13h AH=00 */
bios_disk_reset:
    pushw   %bp
    movw    %sp, %bp
    pushw   %si
    pushw   %di
    pushw   %es
    pushw   %ds

    movb    4(%bp), %dl
    xorw    %ax, %ax
    sti
    int     $0x13
    cli
    cld

    popw    %ds
    popw    %es
    popw    %di
    popw    %si
    popw    %bp
    ret
