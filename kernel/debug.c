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
#include "ui.h"

/* The FLAGS bits that are set, by name */
static void flag_list(uint16_t flags)
{
    static const struct { uint16_t bit; const char *name; } names[] = {
        { 0x0001, "CF" }, { 0x0004, "PF" }, { 0x0010, "AF" }, { 0x0040, "ZF" },
        { 0x0080, "SF" }, { 0x0100, "TF" }, { 0x0200, "IF" }, { 0x0400, "DF" }, { 0x0800, "OF" },
    };
    uint8_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (flags & names[i].bit) {
            ui_pill(names[i].name, names[i].bit == 0x0200 ? TH_GREEN : TH_SURFACE2);
            con_putchar(' ');
        }
    }
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

    ui_panel_open("Register Dump", TH_BLUE, "the shell, right now");
    ui_row();
    ui_reg("AX", ax_val); ui_reg("BX", bx_val); ui_reg("CX", cx_val); ui_reg("DX", dx_val);
    ui_row_end();
    ui_row();
    ui_reg("SP", sp_val); ui_reg("BP", bp_val); ui_reg("SI", si_val); ui_reg("DI", di_val);
    ui_row_end();
    ui_row();
    ui_reg("CS", cs_val); ui_reg("DS", ds_val); ui_reg("ES", es_val); ui_reg("SS", ss_val);
    ui_row_end();
    ui_row_rule();
    ui_row();
    ui_reg("FLAGS", flags_val);
    flag_list(flags_val);
    ui_row_end();
    ui_panel_close();
}

/* A state as a coloured dot and a lower-case word */
static uint8_t state_colour(uint8_t state)
{
    switch (state) {
    case THREAD_RUNNING:  return TH_GREEN;
    case THREAD_READY:    return TH_BLUE;
    case THREAD_BLOCKED:  return TH_PEACH;
    case THREAD_SLEEPING: return TH_OVERLAY;
    default:              return TH_RED;
    }
}

static void state_word(uint8_t state)
{
    static const char *const words[] = { "ready", "running", "blocked", "sleeping", "ended" };

    ui_dot(state_colour(state), state <= THREAD_TERMINATED ? words[state] : "unknown");
}

/* Thread table columns */
#define COL_NAME    8
#define COL_STATE   22
#define COL_PRI     34
#define COL_TICKS   39
#define COL_SHARE   51

void debug_thread_list(void)
{
    char note[24];
    uint32_t total = 0;
    int i;
    tcb_t *tcb;

    for (i = 0; i < MAX_THREADS; i++) {
        tcb = thread_get_tcb(i);
        if (tcb && tcb->active) {
            total += tcb->cpu_ticks;
        }
    }

    ui_panel_open("Thread List", TH_MAUVE, ui_format((uint32_t)thread_count(), " threads", note, sizeof(note)));
    ui_row();
    ui_text(TH_SUBTEXT, "TID");
    ui_pad_to(COL_NAME);      ui_text(TH_SUBTEXT, "NAME");
    ui_pad_to(COL_STATE);     ui_text(TH_SUBTEXT, "STATE");
    ui_pad_to(COL_PRI);       ui_text(TH_SUBTEXT, "PRI");
    ui_pad_to(COL_TICKS);     ui_text(TH_SUBTEXT, "CPU TICKS");
    ui_pad_to(COL_SHARE);     ui_text(TH_SUBTEXT, "SHARE OF CPU");
    ui_row_end();
    ui_row_rule();

    for (i = 0; i < MAX_THREADS; i++) {
        tcb = thread_get_tcb(i);
        if (!tcb || !tcb->active) continue;

        ui_row();
        ui_num(tcb->tid, 3, TH_SUBTEXT);
        ui_pad_to(COL_NAME);
        ui_text(tcb->state == THREAD_RUNNING ? TH_TEXT : TH_SUBTEXT, tcb->name);
        ui_pad_to(COL_STATE);
        state_word(tcb->state);
        ui_pad_to(COL_PRI);
        ui_num(tcb->priority, 3, TH_TEXT);
        ui_pad_to(COL_TICKS);
        ui_num(tcb->cpu_ticks, 9, TH_TEXT);
        ui_pad_to(COL_SHARE);
        ui_meter(tcb->cpu_ticks, total, 16, state_colour(tcb->state) == TH_OVERLAY ? TH_BLUE
                                                                            : state_colour(tcb->state));
        ui_num(total ? tcb->cpu_ticks * 100 / total : 0, 4, TH_TEXT);
        ui_text(TH_SUBTEXT, "%");
        ui_row_end();
    }
    ui_panel_close();
}

/* Stack table columns */
#define COL_KERNEL  22
#define COL_PROGRAM 50

static void stack_use(uint16_t used, uint16_t size)
{
    uint8_t fg = used * 4 > size * 3 ? TH_RED : used * 2 > size ? TH_YELLOW : TH_GREEN;

    ui_num(used, 4, TH_TEXT);
    ui_text(TH_SUBTEXT, " of ");
    ui_num(size, 4, TH_SUBTEXT);
    con_putchar(' ');
    ui_meter(used, size, 12, fg);
}

void debug_stack_list(void)
{
    int i;
    tcb_t *tcb;
    uint16_t kernel, program;

    ui_panel_open("Stack Use", TH_TEAL, "deepest so far, bytes");
    ui_row();
    ui_text(TH_SUBTEXT, "TID");
    ui_pad_to(COL_NAME);      ui_text(TH_SUBTEXT, "NAME");
    ui_pad_to(COL_KERNEL);    ui_text(TH_SUBTEXT, "KERNEL STACK");
    ui_pad_to(COL_PROGRAM);   ui_text(TH_SUBTEXT, "PROGRAM STACK");
    ui_row_end();
    ui_row_rule();

    for (i = 0; i < MAX_THREADS; i++) {
        tcb = thread_get_tcb(i);
        if (!tcb || !thread_stack_peak(i, &kernel, &program)) {
            continue;
        }
        ui_row();
        ui_num(tcb->tid, 3, TH_SUBTEXT);
        ui_pad_to(COL_NAME);
        ui_text(TH_TEXT, tcb->name);
        ui_pad_to(COL_KERNEL);
        stack_use(kernel, tcb->stack_size);
        if (tcb->program) {
            ui_pad_to(COL_PROGRAM);
            stack_use(program, PROG_STACK_SIZE);
        }
        ui_row_end();
    }
    ui_panel_close();
}

void debug_irq_counts(void)
{
    ui_panel_open("Interrupt Counters", TH_PEACH, "since boot");
    ui_row();
    ui_counter("Timer (IRQ0):", irq_timer_count, NULL);
    ui_counter("Keyboard (IRQ1):", irq_keyboard_count, NULL);
    ui_row_end();
    ui_row();
    ui_counter("Syscall (INT80):", irq_syscall_count, NULL);
    ui_counter("Context Switches:", context_switch_count, NULL);
    ui_row_end();
    ui_panel_close();
}

void debug_sched_queue(void)
{
    int i;
    tcb_t *tcb;
    tcb_t *current = thread_get_tcb(sched_current());

    ui_panel_open("Scheduler Queue", TH_BLUE, "highest priority runs next");
    ui_row();
    ui_text(TH_SUBTEXT, "Current: TID ");
    ui_num((uint32_t)sched_current(), 1, TH_TEXT);
    ui_text(TH_SUBTEXT, "  ");
    ui_dot(TH_GREEN, current->name);
    ui_row_end();
    ui_row();
    ui_text(TH_SUBTEXT, "Ready threads:");
    ui_row_end();

    for (i = 0; i < MAX_THREADS; i++) {
        tcb = thread_get_tcb(i);
        if (tcb && tcb->active && tcb->state == THREAD_READY) {
            ui_row();
            ui_pad_to(5);
            ui_num((uint32_t)i, 2, TH_SUBTEXT);
            ui_pad_to(10);
            ui_dot(TH_BLUE, tcb->name);
            ui_pad_to(28);
            ui_text(TH_SUBTEXT, "priority ");
            ui_num(tcb->priority, 1, TH_TEXT);
            if (tcb->eff_priority != tcb->priority) {
                ui_text(TH_SUBTEXT, ", effective ");
                ui_num(tcb->eff_priority, 1, TH_YELLOW);
            }
            ui_row_end();
        }
    }
    ui_panel_close();
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
        con_set_color(TH_TEXT, TH_RED);
        con_println(" (CORRUPTED!)");
        con_set_color(TH_TEXT, TH_BASE);
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
