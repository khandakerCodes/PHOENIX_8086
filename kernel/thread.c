/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Thread Management Implementation
 *
 * Manages thread lifecycle: creation, destruction, suspension,
 * resumption, sleeping, and yielding. Per-thread stacks live inside
 * the kernel data segment so that SS == DS holds for every thread.
 */

#include "thread.h"
#include "scheduler.h"
#include "sync.h"
#include "console.h"
#include "interrupts.h"
#include "kernel.h"
#include "hal.h"
#include "memory.h"
#include "syscall.h"

/* ── TCB table ──────────────────────────────── */
tcb_t tcb_table[MAX_THREADS];

/* Per-thread stacks (in BSS, inside the kernel data segment) */
static uint8_t thread_stacks[MAX_THREADS][THREAD_STACK_SIZE];

/* Currently running thread ID */
static int current_tid = -1;

/* ── Internal helpers ───────────────────────── */

static void str_copy(char *dst, const char *src, int max)
{
    int i;
    for (i = 0; i < max - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ── Public API ─────────────────────────────── */

/*
 * Common thread setup. `segment`:`entry` is where the thread starts
 * executing; kernel threads pass the kernel's own code segment.
 */
static int create(uint16_t segment, uint16_t entry, uint16_t argument, uint8_t priority,
                  const char *name, program_t *program)
{
    int tid;
    uint16_t flags;
    uint16_t stack_base;
    uint16_t *stack_top;
    frame_t *frame;
    tcb_t *tcb;

    flags = hal_irq_save();

    /* Find a free TCB slot (slot 0 is the idle thread) */
    for (tid = 1; tid < MAX_THREADS; tid++) {
        if (!tcb_table[tid].active) {
            break;
        }
    }
    if (tid >= MAX_THREADS) {
        hal_irq_restore(flags);
        return -1;  /* No free slots */
    }

    tcb = &tcb_table[tid];
    stack_base = (uint16_t)&thread_stacks[tid][0];

    /* Initialize TCB */
    tcb->tid            = tid;
    tcb->priority       = priority;
    tcb->eff_priority   = priority;
    tcb->slice_left     = TIME_SLICE_TICKS;
    tcb->wait_ticks     = 0;
    tcb->sleep_until    = 0;
    tcb->wait_sem       = NULL;
    tcb->wait_timed     = false;
    tcb->wait_timed_out = false;
    tcb->cpu_ticks      = 0;
    tcb->last_scheduled = 0;
    tcb->stack_base     = stack_base;
    tcb->stack_size     = THREAD_STACK_SIZE;
    tcb->program        = program;
    if (program) {
        program->threads++;
    }

    if (name) {
        str_copy(tcb->name, name, 12);
    } else {
        tcb->name[0] = 'T';
        tcb->name[1] = '0' + (tid % 10);
        tcb->name[2] = '\0';
    }

    /* Write stack guard value at the bottom of the stack */
    *(uint16_t *)stack_base = STACK_GUARD_VALUE;

    /*
     * Set up the initial stack. From the top down:
     *
     *   return address  → thread_exit, so a kernel thread whose entry
     *                     function simply returns ends cleanly (a
     *                     program ends itself with the exit system call)
     *   frame_t         → the register frame the ISR stub restores
     *                     the first time this thread is scheduled;
     *                     its IRET jumps to entry with interrupts on
     */
    stack_top = (uint16_t *)(stack_base + THREAD_STACK_SIZE);
    *--stack_top = (uint16_t)thread_exit;

    frame = (frame_t *)stack_top - 1;
    frame->es    = KERNEL_DATA_SEG;
    frame->ds    = KERNEL_DATA_SEG;
    frame->di    = 0;
    frame->si    = argument;
    frame->bp    = 0;
    frame->bx    = 0;
    frame->dx    = 0;
    frame->cx    = 0;
    frame->ax    = 0;
    frame->ip    = entry;
    frame->cs    = segment;
    frame->flags = FLAGS_INITIAL;

    tcb->sp     = (uint16_t)frame;
    tcb->ss     = KERNEL_DATA_SEG;
    tcb->state  = THREAD_READY;
    tcb->active = true;

    telemetry_thread_created((uint8_t)tid);

    hal_irq_restore(flags);
    return tid;
}

int thread_create(void (*entry)(void), uint8_t priority, const char *name)
{
    return create(hal_get_cs(), (uint16_t)entry, 0, priority, name, NULL);
}

int thread_create_program(program_t *program, uint16_t entry, uint16_t argument,
                          uint8_t priority, const char *name)
{
    return create(program->segment, entry, argument, priority, name, program);
}

/*
 * Mark a thread as gone and release what it was waiting on.
 * Interrupts must be off. Does not reschedule: if the thread is the
 * current one, the caller must switch away without returning to it.
 */
void thread_terminate(int tid)
{
    tcb_t *tcb = &tcb_table[tid];

    if (tcb->wait_sem) {
        sem_remove_waiter((semaphore_t *)tcb->wait_sem, tid);
        tcb->wait_sem = NULL;
    }

    /*
     * Leave the loaded program, freeing its memory if this was its
     * last thread. Safe even if this is the running thread: it is
     * executing kernel code on its own stack by now and never returns
     * to the program.
     */
    if (tcb->program) {
        program_t *program = tcb->program;

        tcb->program = NULL;
        if (--program->threads == 0) {
            far_free(program->segment);
            kfree(program->data);
            kfree(program);
        }
    }
    file_close_owned(tid);
    far_free_owned(tid);

    tcb->state  = THREAD_TERMINATED;
    tcb->active = false;

    telemetry_thread_exited((uint8_t)tid);
}

void thread_destroy(int tid)
{
    uint16_t flags;

    if (tid <= 0 || tid >= MAX_THREADS) return;  /* The idle thread cannot be destroyed */

    flags = hal_irq_save();

    if (!tcb_table[tid].active) {
        hal_irq_restore(flags);
        return;
    }

    thread_terminate(tid);

    /*
     * If destroying the current thread, switch away for good. Its
     * stack and TCB slot stay untouched until then because
     * interrupts are still off.
     */
    if (tid == current_tid) {
        thread_yield();
    }

    hal_irq_restore(flags);
}

void thread_suspend(int tid)
{
    uint16_t flags;

    if (tid <= 0 || tid >= MAX_THREADS) return;

    flags = hal_irq_save();
    if (tcb_table[tid].active &&
        (tcb_table[tid].state == THREAD_READY ||
         tcb_table[tid].state == THREAD_RUNNING)) {
        tcb_table[tid].state = THREAD_BLOCKED;
        telemetry_thread_state((uint8_t)tid);
        if (tid == current_tid) {
            thread_yield();
        }
    }
    hal_irq_restore(flags);
}

void thread_resume(int tid)
{
    uint16_t flags;

    if (tid <= 0 || tid >= MAX_THREADS) return;

    flags = hal_irq_save();
    /* Threads blocked on a semaphore are woken by that semaphore only */
    if (tcb_table[tid].active &&
        tcb_table[tid].state == THREAD_BLOCKED &&
        tcb_table[tid].wait_sem == NULL) {
        tcb_table[tid].state = THREAD_READY;
        telemetry_thread_state((uint8_t)tid);
    }
    hal_irq_restore(flags);
}

void thread_yield(void)
{
    /*
     * Dedicated software interrupt: saves this thread's context and
     * runs the scheduler, exactly like a timer preemption. Works with
     * interrupts disabled; the caller's flags come back on resume.
     */
    __asm__ __volatile__("int $0x81");
}

void thread_sleep(uint16_t ticks)
{
    uint16_t flags = hal_irq_save();

    sched_sleep(current_tid, ticks);
    thread_yield();

    hal_irq_restore(flags);
}

bool thread_set_priority(int tid, uint8_t priority)
{
    uint16_t flags;
    bool ok = false;

    if (tid <= 0 || tid >= MAX_THREADS) return false;

    flags = hal_irq_save();
    if (tcb_table[tid].active) {
        tcb_table[tid].priority     = priority;
        tcb_table[tid].eff_priority = priority;
        telemetry_thread_state((uint8_t)tid);
        ok = true;
    }
    hal_irq_restore(flags);
    return ok;
}

void thread_exit(void)
{
    thread_destroy(current_tid);

    /* Only reached for the idle thread, which cannot exit */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

tcb_t *thread_get_tcb(int tid)
{
    if (tid < 0 || tid >= MAX_THREADS) return NULL;
    return &tcb_table[tid];
}

int thread_current_tid(void)
{
    return current_tid;
}

void thread_set_current(int tid)
{
    current_tid = tid;
}

int thread_count(void)
{
    int count = 0;
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        if (tcb_table[i].active) {
            count++;
        }
    }
    return count;
}
