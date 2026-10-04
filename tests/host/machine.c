/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Simulated machine for host tests (see machine.h)
 *
 * Provides the kernel symbols the code under test expects from the
 * rest of the kernel: the TCB table and thread calls, interrupt
 * masking (a no-op: nothing preempts), the tick counter, the console
 * (discarded) and the simulated megabyte behind MK_FP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "machine.h"
#include "../../kernel/thread.h"
#include "../../kernel/hal.h"
#include "../../kernel/console.h"

unsigned char host_memory[HOST_MEMORY_SIZE];

/* ── Kernel globals ─────────────────────────── */

volatile uint32_t tick_count;
volatile uint32_t irq_timer_count;
volatile uint32_t irq_keyboard_count;
volatile uint32_t irq_syscall_count;
volatile uint32_t context_switch_count;
uint8_t  boot_drive;
uint16_t mem_kb = 640;

/* ── Threads ────────────────────────────────── */

#define HOST_STACK_SIZE (64 * 1024)

static tcb_t tcbs[MAX_THREADS];
static ucontext_t contexts[MAX_THREADS];
static ucontext_t driver;               /* The test program */
static void (*entries[MAX_THREADS])(void);
static char *stacks[MAX_THREADS];
static int current;                     /* 0 while the test program runs */
static int last_run;

static char log_text[256];
static unsigned log_length;

void host_reset(void)
{
    int i;

    for (i = 0; i < MAX_THREADS; i++) {
        memset(&tcbs[i], 0, sizeof(tcbs[i]));
        tcbs[i].tid = (uint16_t)i;
    }
    /* Thread 0 is the test program, always running */
    tcbs[0].active = true;
    tcbs[0].state = THREAD_RUNNING;
    strcpy(tcbs[0].name, "test");
    current = 0;
    last_run = 0;
    tick_count = 0;
    log_length = 0;
    log_text[0] = '\0';
}

static void thread_start(void)
{
    int tid = current;

    entries[tid]();
    tcbs[tid].active = false;
    tcbs[tid].state = THREAD_TERMINATED;
    /* Returning resumes the driver through uc_link */
}

static int free_slot(void)
{
    int tid;

    for (tid = 1; tid < MAX_THREADS; tid++) {
        if (!tcbs[tid].active) return tid;
    }
    fprintf(stderr, "host_spawn: no free thread slot\n");
    exit(2);
}

/* getcontext returns twice, so it lives in a function of its own */
static void prepare_context(int tid)
{
    if (!stacks[tid]) {
        stacks[tid] = malloc(HOST_STACK_SIZE);
    }
    getcontext(&contexts[tid]);
    contexts[tid].uc_stack.ss_sp = stacks[tid];
    contexts[tid].uc_stack.ss_size = HOST_STACK_SIZE;
    contexts[tid].uc_link = &driver;
    makecontext(&contexts[tid], thread_start, 0);
}

int host_spawn(void (*entry)(void), uint8_t priority, const char *name)
{
    int tid = free_slot();

    memset(&tcbs[tid], 0, sizeof(tcbs[tid]));
    tcbs[tid].tid = (uint16_t)tid;
    tcbs[tid].priority = priority;
    tcbs[tid].eff_priority = priority;
    tcbs[tid].state = THREAD_READY;
    tcbs[tid].active = true;
    snprintf(tcbs[tid].name, sizeof(tcbs[tid].name), "%s", name);
    entries[tid] = entry;
    prepare_context(tid);
    return tid;
}

/* Highest effective priority among READY threads, round-robin among equals */
static int pick(void)
{
    int best = -1;
    int i;

    for (i = 1; i < MAX_THREADS; i++) {
        int tid = 1 + (last_run + i - 1) % (MAX_THREADS - 1);

        if (tcbs[tid].active && tcbs[tid].state == THREAD_READY &&
            (best < 0 || tcbs[tid].eff_priority > tcbs[best].eff_priority)) {
            best = tid;
        }
    }
    return best;
}

unsigned host_run(void)
{
    unsigned switches = 0;
    int tid;

    while ((tid = pick()) >= 0) {
        tcb_t *t = &tcbs[tid];

        /* As sched_switch: the aging boost is spent, an inherited priority is not */
        t->eff_priority = t->priority > t->inherited ? t->priority : t->inherited;
        t->state = THREAD_RUNNING;
        current = tid;
        last_run = tid;
        switches++;
        if (switches > 100000) {
            fprintf(stderr, "host_run: threads never settle\n");
            exit(2);
        }

        swapcontext(&driver, &contexts[tid]);

        current = 0;
        if (t->active && t->state == THREAD_RUNNING) {
            t->state = THREAD_READY;    /* It yielded */
        }
    }
    return switches;
}

tcb_t *host_tcb(int tid)
{
    return &tcbs[tid];
}

void host_log(char c)
{
    if (log_length + 1 < sizeof(log_text)) {
        log_text[log_length++] = c;
        log_text[log_length] = '\0';
    }
}

const char *host_log_text(void)
{
    return log_text;
}

/* ── Kernel thread API used by the code under test ── */

tcb_t *thread_get_tcb(int tid)
{
    return (tid >= 0 && tid < MAX_THREADS) ? &tcbs[tid] : NULL;
}

int thread_current_tid(void)
{
    return current;
}

void thread_yield(void)
{
    if (current == 0) {
        if (tcbs[0].state != THREAD_RUNNING) {
            fprintf(stderr, "the test program blocked; only threads may block\n");
            exit(2);
        }
        return;     /* Nothing else runs until host_run */
    }
    swapcontext(&contexts[current], &driver);
}

uint16_t hal_irq_save(void)
{
    return 0;
}

void hal_irq_restore(uint16_t flags)
{
    (void)flags;
}

uint32_t irq_ticks(void)
{
    return tick_count;
}

/* ── Console: discarded ─────────────────────── */

void con_putchar(char c) { (void)c; }
void con_print(const char *s) { (void)s; }
void con_println(const char *s) { (void)s; }
void con_print_hex(uint16_t v) { (void)v; }
void con_print_dec(uint16_t v) { (void)v; }
void con_set_color(uint8_t fg, uint8_t bg) { (void)fg; (void)bg; }

/* ── Checks ─────────────────────────────────── */

unsigned host_checks;
unsigned host_failures;

void host_check(bool ok, const char *what, const char *file, int line)
{
    host_checks++;
    if (!ok) {
        host_failures++;
        fprintf(stderr, "  FAIL  %s:%d: %s\n", file, line, what);
    }
}

int host_summary(const char *suite)
{
    printf("  %-8s %u checks, %u failed\n", suite, host_checks, host_failures);
    return host_failures ? 1 : 0;
}
