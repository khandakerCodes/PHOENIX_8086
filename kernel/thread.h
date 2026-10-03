/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Thread Management Header
 */

#ifndef PHOENIX_THREAD_H
#define PHOENIX_THREAD_H

#include "../include/types.h"
#include "tcb.h"

/* ── Thread API ─────────────────────────────── */

/*
 * Create a new thread.
 * Returns the thread ID (0..MAX_THREADS-1) on success, -1 on failure.
 *
 * entry:    function pointer to the thread's entry point
 * priority: scheduling priority (0 = lowest, 255 = highest)
 * name:     human-readable name (max 11 chars + null)
 */
int thread_create(void (*entry)(void), uint8_t priority, const char *name);

/*
 * Create a thread inside a loaded program, starting at `entry` in the
 * program's code segment with `argument` in the SI register. The
 * thread holds a reference to the program, whose memory is freed when
 * its last thread ends. Returns the thread ID or -1.
 */
int thread_create_program(program_t *program, uint16_t entry, uint16_t argument,
                          uint8_t priority, const char *name);

/* Destroy a thread by ID */
void thread_destroy(int tid);

/* Suspend a thread (set to BLOCKED) */
void thread_suspend(int tid);

/* Resume a suspended thread (set to READY) */
void thread_resume(int tid);

/* Yield the current thread's time slice */
void thread_yield(void);

/* Put the current thread to sleep for a number of timer ticks */
void thread_sleep(uint16_t ticks);

/* Exit the currently running thread */
void thread_exit(void);

/* Change a thread's base priority; returns false if the thread does not exist */
bool thread_set_priority(int tid, uint8_t priority);

/* Get a pointer to the TCB for a given thread ID */
tcb_t *thread_get_tcb(int tid);

/* Get the currently running thread's ID */
int thread_current_tid(void);

/* Get the total number of active threads */
int thread_count(void);

#endif /* PHOENIX_THREAD_H */
