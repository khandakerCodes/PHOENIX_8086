/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Preemptive Scheduler Implementation
 *
 * Timer-driven preemptive scheduler: strict priority between
 * levels, round-robin with a time slice inside a level, and aging
 * so that low-priority threads are not starved.
 *
 * On each timer tick (sched_tick):
 *   1. Charge the tick to the current thread and its time slice
 *   2. Wake sleepers whose time has come
 *   3. Age READY threads
 *
 * On every interrupt that may change what should run (sched_switch):
 *   1. Save the interrupted thread's stack pointer in its TCB
 *   2. Select the next thread
 *   3. Return that thread's saved stack pointer; the ISR stub in
 *      isr.S restores its registers from there and IRETs into it
 */

#include "scheduler.h"
#include "thread.h"
#include "tcb.h"
#include "console.h"
#include "interrupts.h"
#include "kernel.h"
#include "sync.h"
#include "floppy.h"
#include "hal.h"
#include "telemetry.h"
#include "ui.h"

/* ── External TCB table (defined in thread.c) ─ */
extern tcb_t tcb_table[MAX_THREADS];
extern void thread_set_current(int tid);
extern void thread_terminate(int tid);

/* ── Scheduler state ────────────────────────── */
static int current_thread = -1;

/* ── Sleep queue management ─────────────────── */

static void check_sleep_queue(void)
{
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        if (tcb_table[i].active &&
            tcb_table[i].state == THREAD_SLEEPING) {
            /* Signed difference keeps this correct when tick_count wraps */
            if ((int32_t)(tick_count - tcb_table[i].sleep_until) >= 0) {
                tcb_table[i].state = THREAD_READY;
                telemetry_thread_state((uint8_t)i);
            }
        }

        /* A timed semaphore wait that ran out: wake the thread empty-handed */
        if (tcb_table[i].active &&
            tcb_table[i].state == THREAD_BLOCKED && tcb_table[i].wait_timed &&
            (int32_t)(tick_count - tcb_table[i].sleep_until) >= 0) {
            sem_remove_waiter((semaphore_t *)tcb_table[i].wait_sem, i);
            tcb_table[i].wait_sem = NULL;
            tcb_table[i].wait_timed = false;
            tcb_table[i].wait_timed_out = true;
            tcb_table[i].state = THREAD_READY;
            telemetry_thread_state((uint8_t)i);
        }
    }
}

/* ── Aging ──────────────────────────────────── */

/*
 * Every AGING_TICKS a thread spends READY raises its effective
 * priority by one, so it eventually outranks whatever is hogging
 * the CPU. The boost is dropped when the thread gets to run.
 * The idle thread (TID 0) never ages.
 */
static void age_ready_threads(void)
{
    int i;
    for (i = 1; i < MAX_THREADS; i++) {
        if (tcb_table[i].active && tcb_table[i].state == THREAD_READY) {
            if (++tcb_table[i].wait_ticks >= AGING_TICKS) {
                tcb_table[i].wait_ticks = 0;
                if (tcb_table[i].eff_priority < 255) {
                    tcb_table[i].eff_priority++;
                }
            }
        }
    }
}

/* ── Stack guard check ──────────────────────── */

/* Report a stack overflow and stop the thread; the caller switches away */
static void stack_overflow(int tid, uint8_t kind, uint16_t sp)
{
    con_putchar('\n');
    con_print("  ");
    ui_mark(false);
    ui_text(TH_RED, kind == TEL_TFAULT_PROGRAM_STACK ? "PROGRAM STACK OVERFLOW: Thread "
                                                     : "STACK OVERFLOW: Thread ");
    con_set_color(TH_RED, TH_BASE);
    con_print_dec(tid);
    con_print(" (");
    con_print(tcb_table[tid].name);
    con_print(")");
    ui_text(TH_SUBTEXT, " stopped");
    con_putchar('\n');

    telemetry_thread_fault((uint8_t)tid, kind, sp);
    thread_terminate(tid);
}

/*
 * Two checks per stack: the guard word at the bottom must be intact,
 * and the stack pointer must not have entered the red zone just above
 * it. The red zone catches an overflow before it reaches the
 * neighbouring stack.
 *
 * A program thread has a second stack in its program's data segment.
 * Its last stack pointer there is kept at the top of the thread's
 * kernel stack (user_context_t), whether the thread was interrupted in
 * the program or is in the middle of a system call. Real mode cannot
 * stop the overflow from happening; this notices it at the next switch.
 */
static void check_stack_guard(int tid, uint16_t sp)
{
    tcb_t *tcb;
    uint16_t *guard;

    if (tid < 0 || tid >= MAX_THREADS) return;
    tcb = &tcb_table[tid];
    if (!tcb->active) return;

    guard = (uint16_t *)tcb->stack_base;
    if (*guard != STACK_GUARD_VALUE || sp < tcb->stack_base + STACK_RED_ZONE) {
        stack_overflow(tid, TEL_TFAULT_KERNEL_STACK, sp);
        return;
    }

    if (tcb->program) {
        const program_t *program = tcb->program;
        const user_context_t *user =
            (const user_context_t *)(tcb->stack_base + tcb->stack_size) - 1;
        uint16_t bottom = PROG_STACK_BOTTOM(program, tcb->program_stack);
        uint16_t user_guard = *(const uint16_t __far *)MK_FP(program->data_segment, bottom);

        if (user_guard != STACK_GUARD_VALUE ||
            user->user_sp < bottom + PROG_STACK_RED_ZONE ||
            user->user_sp > bottom + PROG_STACK_SIZE) {
            stack_overflow(tid, TEL_TFAULT_PROGRAM_STACK, user->user_sp);
        }
    }
}

/* ── Thread selection ───────────────────────── */

static bool is_runnable(int tid)
{
    if (!tcb_table[tid].active) return false;
    if (tcb_table[tid].state == THREAD_READY) return true;
    return tid == current_thread && tcb_table[tid].state == THREAD_RUNNING;
}

static int select_next_thread(void)
{
    int best_tid = -1;
    int i, idx;

    /*
     * Scan in round-robin order starting after the current thread,
     * so the current thread is examined last. Only a strictly higher
     * effective priority replaces the best candidate, which makes
     * equal-priority threads take turns.
     */
    for (i = 1; i <= MAX_THREADS; i++) {
        idx = (current_thread + i) % MAX_THREADS;
        if (idx == 0 || !is_runnable(idx)) continue;
        if (best_tid < 0 ||
            tcb_table[idx].eff_priority > tcb_table[best_tid].eff_priority) {
            best_tid = idx;
        }
    }

    /*
     * The current thread keeps the CPU while its time slice lasts,
     * unless a higher-priority thread is ready.
     */
    if (current_thread > 0 && is_runnable(current_thread) &&
        tcb_table[current_thread].slice_left > 0 &&
        tcb_table[current_thread].eff_priority >= tcb_table[best_tid].eff_priority) {
        best_tid = current_thread;
    }

    /* If nothing is runnable, fall back to idle (thread 0) */
    if (best_tid < 0) {
        best_tid = 0;
    }

    return best_tid;
}

/* ── Public API ─────────────────────────────── */

void sched_init(void)
{
    /*
     * The code running right now (kernel_main on the kernel stack)
     * becomes thread 0, the idle thread. It needs no fabricated
     * frame: its registers are saved the first time it is switched out.
     */
    tcb_t *idle = &tcb_table[0];
    const char *name = "idle";
    int i;

    idle->tid          = 0;
    idle->ss           = KERNEL_DATA_SEG;
    idle->state        = THREAD_RUNNING;
    idle->priority     = 0;
    idle->eff_priority = 0;
    idle->slice_left   = 0;
    idle->stack_base   = KERNEL_STACK_TOP + 2 - KERNEL_STACK_SIZE;
    idle->stack_size   = KERNEL_STACK_SIZE;
    idle->active       = true;
    for (i = 0; name[i]; i++) {
        idle->name[i] = name[i];
    }
    idle->name[i] = '\0';

    /*
     * This stack is in use right now, so only the part well below the
     * stack pointer can be marked unused (see thread_stack_peak).
     */
    {
        uint16_t sp;

        __asm__ __volatile__("movw %%sp, %0" : "=r"(sp));
        hal_fill_words(KERNEL_DATA_SEG, idle->stack_base,
                       (sp - 32 - idle->stack_base) / 2, STACK_FILL_WORD);
    }
    *(uint16_t *)idle->stack_base = STACK_GUARD_VALUE;

    current_thread = 0;
    thread_set_current(0);
    telemetry_thread_created(0);
}

void sched_tick(void)
{
    tcb_t *cur = &tcb_table[current_thread];

    /* Update current thread's CPU time and time slice */
    if (cur->active) {
        cur->cpu_ticks++;
        if (cur->slice_left > 0) {
            cur->slice_left--;
        }
    }

    /* Wake up sleeping threads */
    check_sleep_queue();

    /* Stop the floppy motor once the drive has been idle for a while */
    floppy_tick();

    /* Raise the effective priority of threads kept waiting */
    age_ready_threads();
}

void sched_yield_current(void)
{
    tcb_table[current_thread].slice_left = 0;
}

uint16_t sched_switch(uint16_t sp)
{
    int next_tid;
    tcb_t *next;

    /* Save where the interrupted thread's register frame lives */
    tcb_table[current_thread].sp = sp;

    check_stack_guard(current_thread, sp);

    next_tid = select_next_thread();
    next = &tcb_table[next_tid];

    if (next_tid == current_thread) {
        /* Same thread keeps running; start a new slice if this one ran out */
        if (next->slice_left == 0) {
            next->slice_left = TIME_SLICE_TICKS;
        }
        return sp;
    }

    telemetry_context_switch((uint8_t)current_thread, (uint8_t)next_tid, next->sp);

    /* The outgoing thread goes back to READY unless it blocked, slept or died */
    if (tcb_table[current_thread].active &&
        tcb_table[current_thread].state == THREAD_RUNNING) {
        tcb_table[current_thread].state = THREAD_READY;
    }

    next->state          = THREAD_RUNNING;
    /* The aging boost is spent; an inherited priority is not (sync.c) */
    next->eff_priority   = next->priority > next->inherited ? next->priority : next->inherited;
    next->wait_ticks     = 0;
    next->slice_left     = TIME_SLICE_TICKS;
    next->last_scheduled = tick_count;

    context_switch_count++;

    current_thread = next_tid;
    thread_set_current(next_tid);

    return next->sp;
}

void sched_sleep(int tid, uint16_t ticks)
{
    if (tid < 0 || tid >= MAX_THREADS) return;
    if (!tcb_table[tid].active) return;

    tcb_table[tid].state = THREAD_SLEEPING;
    tcb_table[tid].sleep_until = tick_count + ticks;
    telemetry_thread_state((uint8_t)tid);
}

int sched_current(void)
{
    return current_thread;
}
