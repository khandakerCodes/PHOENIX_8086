/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Simulated machine for host tests
 *
 * Just enough of a kernel for sync.c, ipc.c, memory.c and fat12.c to
 * run unmodified on the host:
 *
 *   * Threads are real: each has its own stack (ucontext), so a
 *     semaphore wait really suspends the thread and a signal really
 *     resumes it in the middle of the kernel function.
 *   * Scheduling is cooperative and simple: a thread runs until it
 *     blocks, yields or ends, then the runnable thread with the
 *     highest effective priority goes next (round-robin among equals).
 *     It mirrors the kernel's rules, not its timer-driven preemption;
 *     the real scheduler is tested on the target (make test).
 *   * The test program itself is thread 0. It may call anything that
 *     does not block.
 */

#ifndef PHOENIX_HOST_MACHINE_H
#define PHOENIX_HOST_MACHINE_H

#include "../../include/types.h"
#include "../../kernel/tcb.h"

/* Reset every thread and the tick counter */
void host_reset(void);

/* Create a READY thread; returns its TID */
int host_spawn(void (*entry)(void), uint8_t priority, const char *name);

/* Run threads until none can run; returns how many switches happened */
unsigned host_run(void);

/* The TCB of a thread */
tcb_t *host_tcb(int tid);

/* Record an event in a shared log, for checking the order things happened in */
void host_log(char c);
const char *host_log_text(void);

/* ── Checks ── */

extern unsigned host_checks;
extern unsigned host_failures;

#define CHECK(cond) host_check((cond), #cond, __FILE__, __LINE__)
void host_check(bool ok, const char *what, const char *file, int line);

/* Print a summary; returns the process exit status */
int host_summary(const char *suite);

#endif /* PHOENIX_HOST_MACHINE_H */
