/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Debug Monitor Implementation
 *
 * Provides diagnostic dump functions for registers,
 * memory, threads, interrupts, and the scheduler queue.
 */

#include "debug.h"
#include "console.h"
#include "thread.h"
#include "tcb.h"
#include "interrupts.h"
#include "scheduler.h"
#include "hal.h"

/* Thread state names */
static const char *state_names[] = {
    "READY", "RUNNING", "BLOCKED", "SLEEPING", "TERMINATED"
};

static const char *get_state_name(uint8_t state)
{
    if (state <= THREAD_TERMINATED) {
        return state_names[state];
    }
    return "UNKNOWN";
}

void debug_dump_regs(void)
{
    uint16_t ax_val, bx_val, cx_val, dx_val;
    uint16_t sp_val, bp_val, si_val, di_val;
    uint16_t cs_val, ds_val, es_val, ss_val;
    uint16_t flags_val;

    __asm__ __volatile__(
        "mov %%ax, %0\n\t"
        "mov %%bx, %1\n\t"
        "mov %%cx, %2\n\t"
        "mov %%dx, %3\n\t"
        : "=m"(ax_val), "=m"(bx_val), "=m"(cx_val), "=m"(dx_val)
    );

    __asm__ __volatile__(
        "mov %%sp, %0\n\t"
        "mov %%bp, %1\n\t"
        "mov %%si, %2\n\t"
        "mov %%di, %3\n\t"
        : "=m"(sp_val), "=m"(bp_val), "=m"(si_val), "=m"(di_val)
    );

    __asm__ __volatile__(
        "mov %%cs, %0\n\t"
        "mov %%ds, %1\n\t"
        "mov %%es, %2\n\t"
        "mov %%ss, %3\n\t"
        : "=m"(cs_val), "=m"(ds_val), "=m"(es_val), "=m"(ss_val)
    );

    __asm__ __volatile__(
        "pushf\n\t"
        "pop %0\n\t"
        : "=r"(flags_val)
    );

    con_println("=== Register Dump ===");

    con_print("  AX=");
    con_print_hex(ax_val);
    con_print("  BX=");
    con_print_hex(bx_val);
    con_print("  CX=");
    con_print_hex(cx_val);
    con_print("  DX=");
    con_print_hex(dx_val);
    con_putchar('\n');

    con_print("  SP=");
    con_print_hex(sp_val);
    con_print("  BP=");
    con_print_hex(bp_val);
    con_print("  SI=");
    con_print_hex(si_val);
    con_print("  DI=");
    con_print_hex(di_val);
    con_putchar('\n');

    con_print("  CS=");
    con_print_hex(cs_val);
    con_print("  DS=");
    con_print_hex(ds_val);
    con_print("  ES=");
    con_print_hex(es_val);
    con_print("  SS=");
    con_print_hex(ss_val);
    con_putchar('\n');

    con_print("  FLAGS=");
    con_print_hex(flags_val);
    con_putchar('\n');
}

void debug_thread_list(void)
{
    int i;
    tcb_t *tcb;

    con_println("=== Thread List ===");
    con_println("  TID  Name          State      Pri  CPU Ticks");
    con_println("  ---  ----          -----      ---  ---------");

    for (i = 0; i < MAX_THREADS; i++) {
        tcb = thread_get_tcb(i);
        if (tcb && tcb->active) {
            con_print("  ");
            con_print_dec(tcb->tid);
            con_print("    ");
            con_print(tcb->name);

            /* Pad name to 14 chars */
            int namelen = 0;
            const char *p = tcb->name;
            while (*p) { namelen++; p++; }
            while (namelen < 14) { con_putchar(' '); namelen++; }

            con_print(get_state_name(tcb->state));

            /* Pad state */
            int statelen = 0;
            const char *s = get_state_name(tcb->state);
            while (*s) { statelen++; s++; }
            while (statelen < 11) { con_putchar(' '); statelen++; }

            con_print_dec(tcb->priority);
            con_print("    ");
            con_print_dec((uint16_t)tcb->cpu_ticks);
            con_putchar('\n');
        }
    }
}

/* Print "<used> of <size>" padded to a column */
static void print_usage(uint16_t used, uint16_t size)
{
    con_print_dec(used);
    con_print(" of ");
    con_print_dec(size);
}

void debug_stack_list(void)
{
    int i;
    tcb_t *tcb;
    uint16_t kernel, program;

    con_println("=== Stack Use (deepest so far, bytes) ===");
    con_println("  TID  Name          Kernel stack    Program stack");
    con_println("  ---  ----          ------------    -------------");

    for (i = 0; i < MAX_THREADS; i++) {
        tcb = thread_get_tcb(i);
        if (!tcb || !thread_stack_peak(i, &kernel, &program)) {
            continue;
        }

        con_print("  ");
        con_print_dec(tcb->tid);
        con_print(tcb->tid < 10 ? "    " : "   ");
        con_print(tcb->name);
        {
            int len = 0;
            const char *p = tcb->name;
            while (*p) { len++; p++; }
            while (len < 14) { con_putchar(' '); len++; }
        }
        print_usage(kernel, tcb->stack_size);
        if (tcb->program) {
            con_print("    ");
            print_usage(program, PROG_STACK_SIZE);
        }
        con_putchar('\n');
    }
}

void debug_irq_counts(void)
{
    con_println("=== Interrupt Counters ===");
    con_print("  Timer (IRQ0):     ");
    con_print_dec((uint16_t)irq_timer_count);
    con_putchar('\n');
    con_print("  Keyboard (IRQ1):  ");
    con_print_dec((uint16_t)irq_keyboard_count);
    con_putchar('\n');
    con_print("  Syscall (INT80):  ");
    con_print_dec((uint16_t)irq_syscall_count);
    con_putchar('\n');
    con_print("  Context Switches: ");
    con_print_dec((uint16_t)context_switch_count);
    con_putchar('\n');
}

void debug_sched_queue(void)
{
    int i;
    tcb_t *tcb;

    con_println("=== Scheduler Queue ===");
    con_print("  Current: TID ");
    con_print_dec(sched_current());
    con_putchar('\n');

    con_println("  Ready threads:");
    for (i = 0; i < MAX_THREADS; i++) {
        tcb = thread_get_tcb(i);
        if (tcb && tcb->active && tcb->state == THREAD_READY) {
            con_print("    [");
            con_print_dec(i);
            con_print("] ");
            con_print(tcb->name);
            con_print(" (pri=");
            con_print_dec(tcb->priority);
            con_println(")");
        }
    }
}

void debug_dump_stack(int tid)
{
    tcb_t *tcb = thread_get_tcb(tid);
    if (!tcb || !tcb->active) {
        con_print("Thread ");
        con_print_dec(tid);
        con_println(" not found");
        return;
    }

    con_print("=== Stack Dump: Thread ");
    con_print_dec(tid);
    con_print(" (");
    con_print(tcb->name);
    con_println(") ===");

    con_print("  Base: ");
    con_print_hex(tcb->stack_base);
    con_print("  Size: ");
    con_print_dec(tcb->stack_size);
    con_print("  SP: ");
    con_print_hex(tcb->sp);
    con_putchar('\n');

    /* Check guard */
    uint16_t *guard = (uint16_t *)tcb->stack_base;
    con_print("  Guard: ");
    con_print_hex(*guard);
    if (*guard == STACK_GUARD_VALUE) {
        con_println(" (OK)");
    } else {
        con_set_color(VGA_WHITE, VGA_RED);
        con_println(" (CORRUPTED!)");
        con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    }
}

void debug_dump_mem(uint16_t seg, uint16_t off, uint16_t len)
{
    /* Simplified — just print first 64 bytes */
    uint16_t i;
    uint8_t __far *ptr;

    if (len > 64) len = 64;

    con_print("=== Memory Dump ");
    con_print_hex(seg);
    con_print(":");
    con_print_hex(off);
    con_println(" ===");

    ptr = (uint8_t __far *)MK_FP(seg, off);

    for (i = 0; i < len; i++) {
        if (i % 16 == 0) {
            con_print("  ");
            con_print_hex(off + i);
            con_print(": ");
        }

        /* Print hex byte */
        static const char hex[] = "0123456789ABCDEF";
        con_putchar(hex[(ptr[i] >> 4) & 0xF]);
        con_putchar(hex[ptr[i] & 0xF]);
        con_putchar(' ');

        if ((i + 1) % 16 == 0) {
            con_putchar('\n');
        }
    }
    if (len % 16 != 0) {
        con_putchar('\n');
    }
}
