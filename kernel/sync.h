/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Synchronization Primitives Header
 */

#ifndef PHOENIX_SYNC_H
#define PHOENIX_SYNC_H

#include "../include/types.h"

/* ── Semaphore ──────────────────────────────── */
typedef struct {
    int16_t count;                      /* Negative: number of waiters */
    uint8_t wait_queue[MAX_THREADS];    /* Blocked thread IDs, oldest first */
    uint8_t wait_count;
} semaphore_t;

void sem_init(semaphore_t *s, int16_t initial);

/* Take the semaphore; blocks the calling thread until it is available */
void sem_wait(semaphore_t *s);

/*
 * Take the semaphore, waiting at most `ticks` timer ticks.
 * Returns false if the time ran out first.
 */
bool sem_wait_timeout(semaphore_t *s, uint16_t ticks);

/* Take the semaphore if available; returns false instead of blocking */
bool sem_trywait(semaphore_t *s);

/* Release the semaphore and wake the oldest waiter. Safe from ISRs. */
void sem_signal(semaphore_t *s);

/*
 * Like sem_signal, but wakes the thread that started waiting most
 * recently instead of the one that has waited longest.
 */
void sem_signal_newest(semaphore_t *s);

/* Drop a thread from the wait queue (used when a blocked thread is killed) */
void sem_remove_waiter(semaphore_t *s, int tid);

/* ── Mutex ──────────────────────────────────── */
typedef struct {
    semaphore_t sem;  /* Binary semaphore; waiters block on it */
    int         owner;/* TID of the owner, -1 if unlocked */
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

/* ── Spinlock ───────────────────────────────── */
void spinlock_acquire(volatile bool *lock);
void spinlock_release(volatile bool *lock);

#endif /* PHOENIX_SYNC_H */
