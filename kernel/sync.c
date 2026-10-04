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

bool sem_wait_timeout(semaphore_t *s, uint16_t ticks)
{
    uint16_t flags = hal_irq_save();
    bool taken = true;

    s->count--;

    if (s->count < 0) {
        int tid = thread_current_tid();
        tcb_t *tcb = thread_get_tcb(tid);

        s->wait_queue[s->wait_count++] = (uint8_t)tid;
        tcb->wait_sem = s;
        tcb->wait_timed = true;
        tcb->wait_timed_out = false;
        tcb->sleep_until = tick_count + ticks;
        tcb->state = THREAD_BLOCKED;
        telemetry_thread_state((uint8_t)tid);

        /* Resumes after sem_signal, or after the scheduler gives up waiting */
        thread_yield();

        taken = !tcb->wait_timed_out;
        tcb->wait_timed = false;
    }

    hal_irq_restore(flags);
    return taken;
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

/* Release the semaphore and wake the waiter at `index` in the queue, if any */
static void signal_waiter(semaphore_t *s, bool newest)
{
    uint16_t flags = hal_irq_save();

    s->count++;

    if (s->wait_count > 0) {
        uint8_t index = newest ? s->wait_count - 1 : 0;
        int tid = s->wait_queue[index];
        tcb_t *tcb = thread_get_tcb(tid);
        int i;

        /* Close the gap in the wait queue */
        for (i = index + 1; i < s->wait_count; i++) {
            s->wait_queue[i - 1] = s->wait_queue[i];
        }
        s->wait_count--;

        tcb->wait_sem = NULL;
        tcb->wait_timed = false;
        tcb->state = THREAD_READY;
        telemetry_thread_state((uint8_t)tid);
    }

    hal_irq_restore(flags);
}

void sem_signal(semaphore_t *s)
{
    signal_waiter(s, false);
}

void sem_signal_newest(semaphore_t *s)
{
    signal_waiter(s, true);
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

/*
 * Priority inheritance. Without it, a low-priority thread holding a
 * mutex that a high-priority thread wants can be kept off the CPU
 * indefinitely by any medium-priority thread: priority inversion.
 *
 * So a thread that blocks on a mutex lends its priority to the owner,
 * and, if the owner is itself waiting for another mutex, to that
 * mutex's owner, and so on (INHERIT_DEPTH levels; a longer chain on
 * eight threads would be a deadlock anyway). As in FreeRTOS, an owner
 * keeps what it inherited until it has released every mutex it holds:
 * simpler than undoing each loan separately, and never too low.
 */
#define INHERIT_DEPTH   4

/* Interrupts must be off */
static void inherit(mutex_t *m, uint8_t priority, int from)
{
    uint8_t depth;

    for (depth = 0; depth < INHERIT_DEPTH && m != NULL && m->owner >= 0; depth++) {
        tcb_t *owner = thread_get_tcb(m->owner);

        if (owner->inherited >= priority) {
            break;          /* Already lent this much, here and further down the chain */
        }
        owner->inherited = priority;
        if (owner->eff_priority < priority) {
            owner->eff_priority = priority;
        }
        telemetry_priority((uint8_t)m->owner, owner->eff_priority, TEL_PRIO_INHERIT, (uint8_t)from);

        m = (mutex_t *)owner->wait_mutex;
    }
}

void mutex_init(mutex_t *m)
{
    sem_init(&m->sem, 1);
    m->owner = -1;
}

void mutex_lock(mutex_t *m)
{
    uint16_t flags = hal_irq_save();
    int tid = thread_current_tid();
    tcb_t *me = thread_get_tcb(tid);

    if (m->owner >= 0) {
        me->wait_mutex = m;
        inherit(m, me->eff_priority, tid);
    }
    sem_wait(&m->sem);      /* Blocks until the owner lets go */

    me->wait_mutex = NULL;
    m->owner = tid;
    me->mutexes_held++;

    hal_irq_restore(flags);
}

void mutex_unlock(mutex_t *m)
{
    uint16_t flags = hal_irq_save();
    int tid = thread_current_tid();
    tcb_t *me = thread_get_tcb(tid);
    bool restored = false;

    if (m->owner == tid) {
        m->owner = -1;
        if (me->mutexes_held > 0 && --me->mutexes_held == 0 && me->inherited) {
            me->inherited = 0;
            me->eff_priority = me->priority;
            telemetry_priority((uint8_t)tid, me->eff_priority, TEL_PRIO_RESTORE, 0xFF);
            restored = true;
        }
        sem_signal(&m->sem);
    }

    hal_irq_restore(flags);

    /* The thread we were standing in for probably outranks us now */
    if (restored) {
        thread_yield();
    }
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
