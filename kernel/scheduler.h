/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Scheduler Header
 */

#ifndef PHOENIX_SCHEDULER_H
#define PHOENIX_SCHEDULER_H

#include "../include/types.h"

/* Time slice: ticks a thread runs before equal-priority threads get a turn */
#define TIME_SLICE_TICKS    5

/* A READY thread gains one effective priority level per this many ticks */
#define AGING_TICKS         10

/* A thread whose stack pointer drops into the lowest bytes of its stack is killed */
#define STACK_RED_ZONE      192

/* ── Scheduler API ──────────────────────────── */

/* Initialize the scheduler (adopts the boot context as the idle thread) */
void sched_init(void);

/* Per-tick accounting; called from the timer ISR */
void sched_tick(void);

/*
 * Save the current thread's stack pointer, pick the thread to run
 * next, and return its saved stack pointer. Interrupts must be off.
 * Called from the ISR handlers only (see isr.S).
 */
uint16_t sched_switch(uint16_t sp);

/* Give up the rest of the current thread's time slice (ISR context) */
void sched_yield_current(void);

/* Put a thread to sleep for N ticks (interrupts must be off) */
void sched_sleep(int tid, uint16_t ticks);

/* Get the ID of the currently running thread */
int sched_current(void);

#endif /* PHOENIX_SCHEDULER_H */
