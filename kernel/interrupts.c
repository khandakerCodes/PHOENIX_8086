/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Interrupt Management Implementation
 *
 * Installs handlers in the real-mode IVT, manages PIC,
 * and provides the C-side interrupt handlers.
 */

#include "interrupts.h"
#include "ui.h"
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

/* ── Programmable interval timer ───────────── */

/*
 * PIT channel 0, low byte then high byte, in `mode`; divisor 0 means
 * 65536. The kernel uses mode 2 (rate generator): the counter runs
 * down once per tick, from the divisor to 1, so reading it tells how
 * far into the tick we are (timer_counts). In the BIOS's mode 3 it
 * runs down twice per tick, two at a time, and a reading is ambiguous.
 */
#define PIT_MODE_RATE       0x34    /* Mode 2 */
#define PIT_MODE_SQUARE     0x36    /* Mode 3, as the BIOS sets it */
#define PIT_LATCH_CHANNEL0  0x00
#define PIC_READ_IRR        0x0A

static void pit_program(uint8_t mode, uint16_t divisor)
{
    outb(PIT_COMMAND, mode);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}

/* Counts left before the next tick; interrupts must be off */
static uint16_t pit_read(void)
{
    uint8_t low, high;

    outb(PIT_COMMAND, PIT_LATCH_CHANNEL0);
    low = inb(PIT_CHANNEL0);
    high = inb(PIT_CHANNEL0);
    return (uint16_t)low | ((uint16_t)high << 8);
}

uint16_t timer_counts(void)
{
    static uint32_t last_tick;
    static uint16_t last_counts;
    uint16_t elapsed = PIT_TICK_COUNTS - pit_read();
    uint8_t pending;

    /*
     * If the counter has already wrapped but the tick interrupt is
     * still waiting (interrupts are off), the reading belongs to the
     * next tick. Report it as past the end of this one, so that
     * tick_count and the counts together never go backwards.
     */
    outb(PIC1_CMD, PIC_READ_IRR);
    pending = inb(PIC1_CMD) & 0x01;
    if (pending && elapsed < PIT_TICK_COUNTS / 2) {
        elapsed += PIT_TICK_COUNTS;
    }

    /*
     * Within one tick the counter only runs down, so a reading below
     * the last one means it wrapped before the interrupt was raised.
     * Real hardware raises IRQ0 as it wraps; QEMU can lag (seen in
     * about one switch in 500).
     */
    if (tick_count == last_tick && elapsed < last_counts) {
        elapsed = (elapsed + PIT_TICK_COUNTS >= last_counts) ? elapsed + PIT_TICK_COUNTS : last_counts;
    }
    last_tick = tick_count;
    last_counts = elapsed;
    return elapsed;
}

/* ── Timer interrupt latency (bench) ────────── */

static volatile bool latency_on;
static volatile uint16_t latency_min, latency_max, latency_samples;
static volatile uint32_t latency_sum;

void timer_latency_start(void)
{
    uint16_t flags = hal_irq_save();

    latency_min = 0xFFFF;
    latency_max = 0;
    latency_sum = 0;
    latency_samples = 0;
    latency_on = true;
    hal_irq_restore(flags);
}

uint16_t timer_latency_stop(uint16_t *min, uint16_t *average, uint16_t *max)
{
    uint16_t flags = hal_irq_save();
    uint16_t samples = latency_samples;

    latency_on = false;
    *min = samples ? latency_min : 0;
    *max = latency_max;
    *average = samples ? (uint16_t)(latency_sum / samples) : 0;
    hal_irq_restore(flags);
    return samples;
}

/* ── Timer ISR C handler ────────────────────── */

/*
 * Called from the assembly ISR stub in isr.S.
 * Increments tick counter, then invokes the scheduler
 * for preemptive context switching.
 */
uint16_t timer_handler(uint16_t sp)
{
    /*
     * The counter reloaded when it raised IRQ0, so its reading now is
     * how long the interrupt took to reach this handler (bench).
     */
    if (latency_on) {
        uint16_t latency = PIT_TICK_COUNTS - pit_read();

        if (latency < latency_min) latency_min = latency;
        if (latency > latency_max) latency_max = latency;
        latency_sum += latency;
        latency_samples++;
    }

    tick_count++;
    irq_timer_count++;
    ui_timer_tick();        /* The status bar redraws once a second */

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
    telemetry_syscall((uint8_t)sched_current(), (uint8_t)(CONTEXT_FRAME(sp)->ax >> 8));

    if (syscall_dispatch(CONTEXT_FRAME(sp))) {
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
static uint16_t bios_floppy_vector[2];
static uint8_t  bios_pic_mask;

/* Interrupt lines the kernel listens to: bit clear = enabled */
static uint8_t kernel_pic_mask = 0xFC;      /* IRQ0 (timer) and IRQ1 (keyboard) */

/* Handler a driver installed for the floppy line, or NULL */
static void (*floppy_irq_handler)(void);

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

/* The kernel's own setup: HZ ticks, our handlers, only the lines we use */
static void kernel_irq_setup(void)
{
    pit_program(PIT_MODE_RATE, PIT_TICK_COUNTS);

    idt_install(IRQ0_VECTOR, timer_isr);
    idt_install(IRQ1_VECTOR, keyboard_isr);
    if (floppy_irq_handler) {
        idt_install(FLOPPY_VECTOR, floppy_irq_handler);
    }

    outb(PIC1_DATA, kernel_pic_mask);
    outb(PIC2_DATA, 0xFF);   /* All masked on PIC2 */
}

void irq_claim(uint8_t irq, uint8_t vector, void (*handler)(void))
{
    uint16_t flags = hal_irq_save();

    if (vector == FLOPPY_VECTOR) {
        floppy_irq_handler = handler;
    }
    idt_install(vector, handler);
    kernel_pic_mask &= (uint8_t)~(1 << irq);
    outb(PIC1_DATA, kernel_pic_mask);

    hal_irq_restore(flags);
}

void irq_release(uint8_t irq, uint8_t vector)
{
    uint16_t flags = hal_irq_save();

    kernel_pic_mask |= (uint8_t)(1 << irq);
    outb(PIC1_DATA, kernel_pic_mask);
    if (vector == FLOPPY_VECTOR) {
        floppy_irq_handler = NULL;
        ivt_write(FLOPPY_VECTOR, bios_floppy_vector);
    }

    hal_irq_restore(flags);
}

void irq_bios_enter(void)
{
    /* BIOS timing expects its 18.2 Hz tick; its floppy code needs IRQ6 */
    pit_program(PIT_MODE_SQUARE, 0);
    ivt_write(IRQ0_VECTOR, bios_timer_vector);
    ivt_write(IRQ1_VECTOR, bios_keyboard_vector);
    ivt_write(FLOPPY_VECTOR, bios_floppy_vector);
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
    ivt_read(FLOPPY_VECTOR, bios_floppy_vector);
    bios_pic_mask = inb(PIC1_DATA);

    kernel_irq_setup();
    idt_install(SYSCALL_VECTOR, syscall_isr);
    idt_install(YIELD_VECTOR, yield_isr);

    irq_enable();
}
