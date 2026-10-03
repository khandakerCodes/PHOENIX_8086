/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Synchronization Primitives Implementation
 *
 * Semaphore, mutex, and spinlock for thread synchronization
 * in the preemptive kernel. Waiting threads are BLOCKED and taken
 * off the CPU; nothing here busy-waits except the spinlock.
 */

#include "sync.h"
#include "thread.h"
#include "scheduler.h"
#include "interrupts.h"
#include "hal.h"
#include "telemetry.h"

/* ── Semaphore ──────────────────────────────── */

void sem_init(semaphore_t *s, int16_t initial)
{
    s->count = initial;
    s->wait_count = 0;
}

void sem_wait(semaphore_t *s)
{
    uint16_t flags = hal_irq_save();

    s->count--;

    if (s->count < 0) {
        /*
         * Block the current thread. A thread waits on at most one
         * semaphore, so the queue can never hold more than
         * MAX_THREADS entries.
         */
        int tid = thread_current_tid();
        tcb_t *tcb = thread_get_tcb(tid);

        s->wait_queue[s->wait_count++] = (uint8_t)tid;
        tcb->wait_sem = s;
        tcb->state = THREAD_BLOCKED;
        telemetry_thread_state((uint8_t)tid);

        /* Switch away; execution continues here after sem_signal */
        thread_yield();
    }

    hal_irq_restore(flags);
}

bool sem_trywait(semaphore_t *s)
{
    uint16_t flags = hal_irq_save();
    bool taken = false;

    if (s->count > 0) {
        s->count--;
        taken = true;
    }

    hal_irq_restore(flags);
    return taken;
}

void sem_signal(semaphore_t *s)
{
    uint16_t flags = hal_irq_save();

    s->count++;

    if (s->wait_count > 0) {
        /* Wake up the first waiting thread */
        int tid = s->wait_queue[0];
        tcb_t *tcb = thread_get_tcb(tid);
        int i;

        /* Shift wait queue */
        for (i = 1; i < s->wait_count; i++) {
            s->wait_queue[i - 1] = s->wait_queue[i];
        }
        s->wait_count--;

        tcb->wait_sem = NULL;
        tcb->state = THREAD_READY;
        telemetry_thread_state((uint8_t)tid);
    }

    hal_irq_restore(flags);
}

void sem_remove_waiter(semaphore_t *s, int tid)
{
    uint16_t flags = hal_irq_save();
    int i, j;

    for (i = 0; i < s->wait_count; i++) {
        if (s->wait_queue[i] == tid) {
            for (j = i + 1; j < s->wait_count; j++) {
                s->wait_queue[j - 1] = s->wait_queue[j];
            }
            s->wait_count--;
            s->count++;     /* Undo the waiter's decrement */
            break;
        }
    }

    hal_irq_restore(flags);
}

/* ── Mutex ──────────────────────────────────── */

void mutex_init(mutex_t *m)
{
    sem_init(&m->sem, 1);
    m->owner = -1;
}

void mutex_lock(mutex_t *m)
{
    sem_wait(&m->sem);
    m->owner = thread_current_tid();
}

void mutex_unlock(mutex_t *m)
{
    uint16_t flags = hal_irq_save();

    if (m->owner == thread_current_tid()) {
        m->owner = -1;
        sem_signal(&m->sem);
    }

    hal_irq_restore(flags);
}

/* ── Spinlock ───────────────────────────────── */

void spinlock_acquire(volatile bool *lock)
{
    while (true) {
        uint16_t flags = hal_irq_save();
        if (!*lock) {
            *lock = true;
            hal_irq_restore(flags);
            return;
        }
        hal_irq_restore(flags);
        /* Let the holder run instead of burning the time slice */
        thread_yield();
    }
}

void spinlock_release(volatile bool *lock)
{
    *lock = false;
}
