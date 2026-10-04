/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Debug Monitor Header
 */

#ifndef PHOENIX_DEBUG_H
#define PHOENIX_DEBUG_H

#include "../include/types.h"

/* Dump current CPU registers */
void debug_dump_regs(void);

/* Dump memory contents (hex dump) */
void debug_dump_mem(uint16_t seg, uint16_t off, uint16_t len);

/* Dump a thread's stack */
void debug_dump_stack(int tid);

/* Print the thread list with details */
void debug_thread_list(void);

/* Print interrupt counters */
/* Deepest use of every thread's stacks */
void debug_stack_list(void);

void debug_irq_counts(void);

/* Print the scheduler ready queue */
void debug_sched_queue(void);

#endif /* PHOENIX_DEBUG_H */
