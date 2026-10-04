/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Interrupt Management Header
 *
 * Provides IVT installation, PIC control, and
 * interrupt handler management for the kernel.
 */

#ifndef PHOENIX_INTERRUPTS_H
#define PHOENIX_INTERRUPTS_H

#include "../include/types.h"

/* PIC ports */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

/* PIC commands */
#define PIC_EOI     0x20        /* End of Interrupt */

/* Interrupt vectors */
#define IRQ0_VECTOR     0x08    /* Timer */
#define IRQ1_VECTOR     0x09    /* Keyboard */
#define FLOPPY_VECTOR   0x0E    /* Floppy controller (IRQ6) */
#define SYSCALL_VECTOR  0x80    /* Software interrupt for system calls */
#define YIELD_VECTOR    0x81    /* Software interrupt for voluntary yield */

/* PIT ports */
#define PIT_CHANNEL0    0x40
#define PIT_COMMAND     0x43

/* ── Interrupt counters (global) ────────────── */
extern volatile uint32_t tick_count;
extern volatile uint32_t irq_timer_count;
extern volatile uint32_t irq_keyboard_count;
extern volatile uint32_t irq_syscall_count;
extern volatile uint32_t context_switch_count;

/* ── API ────────────────────────────────────── */

/* Initialize interrupt system: install handlers, configure PIC */
void irq_init(void);

/* Install a handler at the given IVT vector */
void idt_install(uint8_t vector, void (*handler)(void));

/* Send End-of-Interrupt to PIC */
void irq_eoi(uint8_t irq);

/* Enable/disable interrupts */
void irq_enable(void);
void irq_disable(void);

/*
 * Let a driver take an interrupt line: install its handler stub and
 * unmask the line. irq_release gives the line back to the BIOS.
 */
void irq_claim(uint8_t irq, uint8_t vector, void (*handler)(void));
void irq_release(uint8_t irq, uint8_t vector);

/*
 * Hand the timer and keyboard back to the BIOS so a BIOS service can
 * run, and take them again afterwards. Interrupts must be off around
 * both calls; nothing is scheduled in between.
 */
void irq_bios_enter(void);
void irq_bios_leave(void);

/* Read the 32-bit tick counter atomically (for use outside ISRs) */
uint32_t irq_ticks(void);

/* PIT input clock counts in one tick (1,193,182 counts per second) */
#define PIT_TICK_COUNTS     ((uint16_t)(PIT_FREQUENCY / HZ))

/*
 * PIT counts since the current tick began (tick_count), about 0.84 us
 * each. Can exceed PIT_TICK_COUNTS when the next tick's interrupt is
 * already pending. Interrupts must be off.
 */
uint16_t timer_counts(void);

/*
 * Measure timer interrupt latency: from the PIT raising IRQ0 to the
 * timer handler starting, in PIT counts. Returns the number of samples.
 */
void timer_latency_start(void);
uint16_t timer_latency_stop(uint16_t *min, uint16_t *average, uint16_t *max);

/*
 * ISR C handlers — called from the stubs in isr.S.
 * Each receives the stack pointer of the saved frame and returns
 * the stack pointer of the frame to resume (see isr.S).
 */
uint16_t timer_handler(uint16_t sp);
uint16_t keyboard_handler(uint16_t sp);
uint16_t syscall_handler(uint16_t sp);
uint16_t yield_handler(uint16_t sp);

#endif /* PHOENIX_INTERRUPTS_H */
