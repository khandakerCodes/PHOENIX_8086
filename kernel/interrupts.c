/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Interrupt Management Implementation
 *
 * Installs handlers in the real-mode IVT, manages PIC,
 * and provides the C-side interrupt handlers.
 */

#include "interrupts.h"
#include "console.h"
#include "keyboard.h"
#include "scheduler.h"
#include "kernel.h"
#include "hal.h"
#include "syscall.h"
#include "tcb.h"

/* ── Global interrupt counters ──────────────── */
volatile uint32_t tick_count         = 0;
volatile uint32_t irq_timer_count    = 0;
volatile uint32_t irq_keyboard_count = 0;
volatile uint32_t irq_syscall_count  = 0;
volatile uint32_t context_switch_count = 0;

/* ── IVT installation ───────────────────────── */

/*
 * Install a handler into the real-mode Interrupt Vector Table.
 * Each IVT entry is 4 bytes: offset (16-bit) + segment (16-bit)
 * at physical address vector * 4.
 */
void idt_install(uint8_t vector, void (*handler)(void))
{
    /* IVT starts at 0000:0000, outside the kernel data segment */
    uint16_t __far *entry = (uint16_t __far *)MK_FP(0x0000, (uint16_t)vector * 4);

    entry[0] = (uint16_t)handler;   /* Offset */
    entry[1] = hal_get_cs();        /* Segment (kernel code segment) */
}

/* ── PIC End of Interrupt ───────────────────── */
void irq_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

/* ── Enable / Disable interrupts ────────────── */
void irq_enable(void)
{
    __asm__ __volatile__("sti");
}

void irq_disable(void)
{
    __asm__ __volatile__("cli");
}

/* ── Atomic tick read ───────────────────────── */

/*
 * tick_count is 32 bits wide and the CPU updates it 16 bits at a
 * time, so a read outside an ISR must not be interrupted halfway.
 */
uint32_t irq_ticks(void)
{
    uint16_t flags = hal_irq_save();
    uint32_t now = tick_count;

    hal_irq_restore(flags);
    return now;
}

/* ── Timer ISR C handler ────────────────────── */

/*
 * Called from the assembly ISR stub in isr.S.
 * Increments tick counter, then invokes the scheduler
 * for preemptive context switching.
 */
uint16_t timer_handler(uint16_t sp)
{
    tick_count++;
    irq_timer_count++;

    sched_tick();

    /* Acknowledge before switching: the next thread must see IRQ0 again */
    irq_eoi(0);

    return sched_switch(sp);
}

/* ── Keyboard ISR C handler ─────────────────── */
uint16_t keyboard_handler(uint16_t sp)
{
    uint8_t scancode = inb(0x60);

    irq_keyboard_count++;

    /* Pass scancode to keyboard driver (may wake a blocked reader) */
    kb_handle_scancode(scancode);

    irq_eoi(1);

    return sched_switch(sp);
}

/* ── Syscall ISR C handler ──────────────────── */
uint16_t syscall_handler(uint16_t sp)
{
    irq_syscall_count++;
    telemetry_syscall((uint8_t)sched_current(), (uint8_t)(((frame_t *)sp)->ax >> 8));

    if (syscall_dispatch((frame_t *)sp)) {
        return sched_switch(sp);
    }
    return sp;
}

/* ── Yield ISR C handler ────────────────────── */
uint16_t yield_handler(uint16_t sp)
{
    sched_yield_current();
    return sched_switch(sp);
}

/* ── ISR stubs (defined in isr.S) ───────────── */
extern void timer_isr(void);
extern void keyboard_isr(void);
extern void syscall_isr(void);
extern void yield_isr(void);

/* ── Timer and interrupt controller setup ───── */

/* What the BIOS had installed before the kernel took over */
static uint16_t bios_timer_vector[2];
static uint16_t bios_keyboard_vector[2];
static uint8_t  bios_pic_mask;

static void ivt_read(uint8_t vector, uint16_t *saved)
{
    uint16_t __far *entry = (uint16_t __far *)MK_FP(0x0000, (uint16_t)vector * 4);

    saved[0] = entry[0];
    saved[1] = entry[1];
}

static void ivt_write(uint8_t vector, const uint16_t *saved)
{
    uint16_t __far *entry = (uint16_t __far *)MK_FP(0x0000, (uint16_t)vector * 4);

    entry[0] = saved[0];
    entry[1] = saved[1];
}

static void pit_set_divisor(uint16_t divisor)
{
    /* PIT channel 0, mode 3 (square wave); divisor 0 means 65536 */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}

/* The kernel's own setup: HZ ticks, our handlers, only IRQ0 and IRQ1 */
static void kernel_irq_setup(void)
{
    pit_set_divisor((uint16_t)(PIT_FREQUENCY / HZ));

    idt_install(IRQ0_VECTOR, timer_isr);
    idt_install(IRQ1_VECTOR, keyboard_isr);

    outb(PIC1_DATA, 0xFC);   /* 11111100 — only IRQ0 and IRQ1 unmasked */
    outb(PIC2_DATA, 0xFF);   /* All masked on PIC2 */
}

void irq_bios_enter(void)
{
    /* BIOS timing expects its 18.2 Hz tick; the floppy needs IRQ6 */
    pit_set_divisor(0);
    ivt_write(IRQ0_VECTOR, bios_timer_vector);
    ivt_write(IRQ1_VECTOR, bios_keyboard_vector);
    outb(PIC1_DATA, bios_pic_mask & ~0x41);
}

void irq_bios_leave(void)
{
    kernel_irq_setup();
}

/* ── Initialize interrupt system ────────────── */
void irq_init(void)
{
    irq_disable();

    /* Remember the BIOS setup so disk reads can borrow it back */
    ivt_read(IRQ0_VECTOR, bios_timer_vector);
    ivt_read(IRQ1_VECTOR, bios_keyboard_vector);
    bios_pic_mask = inb(PIC1_DATA);

    kernel_irq_setup();
    idt_install(SYSCALL_VECTOR, syscall_isr);
    idt_install(YIELD_VECTOR, yield_isr);

    irq_enable();
}
