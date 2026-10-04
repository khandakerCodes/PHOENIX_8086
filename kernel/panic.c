/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Kernel Panic and CPU Exception Traps
 *
 * Displays a red panic screen with the live register state at the
 * point of failure, current thread info, and a reason string, then
 * halts the CPU permanently.
 *
 * Both paths arrive here with a register frame captured by an ISR
 * stub: CPU exceptions (divide error, single-step, breakpoint,
 * overflow) through their own vectors, and kernel_panic() through
 * the dedicated INT 82h.
 */

#include "panic.h"
#include "console.h"
#include "ui.h"
#include "thread.h"
#include "interrupts.h"
#include "kernel.h"

/* Reason passed from kernel_panic() to its interrupt handler */
static const char *panic_reason;

/* ── Panic screen ───────────────────────────── */

static void panic_show(const char *reason, uint16_t context)
{
    frame_t *frame = CONTEXT_FRAME(context);
    tcb_t *tcb = NULL;
    int tid;

    /* Disable all interrupts — we're done */
    irq_disable();

    tid = thread_current_tid();
    telemetry_fault((uint8_t)(tid < 0 ? 0xFF : tid), frame, CONTEXT_RESUME_SP(context), reason);

    /* A dark crash report: the reason in a red pill, then where and the registers */
    ui_status_halted();
    con_reset_color();
    con_clear();
    con_putchar('\n');
    con_print("  ");
    ui_pill(" KERNEL PANIC ", TH_RED);
    con_print("  ");
    ui_text(TH_RED, reason);
    con_putchar('\n');
    con_putchar('\n');

    ui_panel_open("Where", TH_PEACH, NULL);
    ui_row();
    ui_text(TH_SUBTEXT, "Current Thread: ");
    if (tid >= 0) {
        tcb = thread_get_tcb(tid);
        ui_text(TH_SUBTEXT, "TID=");
        ui_num((uint32_t)tid, 1, TH_TEXT);
        if (tcb) {
            ui_text(TH_SUBTEXT, "  Name=");
            ui_text(TH_TEXT, tcb->name);
            ui_text(TH_SUBTEXT, "  State=");
            ui_num(tcb->state, 1, TH_TEXT);
        }
    } else {
        ui_text(TH_TEXT, "None");
    }
    ui_row_end();
    ui_row();
    ui_counter("Timer ticks:", tick_count, NULL);
    ui_counter("Context switches:", context_switch_count, NULL);
    ui_row_end();
    ui_panel_close();

    /* The frame captured when the panic was raised */
    ui_panel_open("Register Dump", TH_PEACH, "at the moment of the panic");
    ui_row();
    ui_reg("AX", frame->ax); ui_reg("BX", frame->bx); ui_reg("CX", frame->cx); ui_reg("DX", frame->dx);
    ui_row_end();
    ui_row();
    ui_reg("SP", CONTEXT_RESUME_SP(context));   /* SP before the interrupt */
    ui_reg("BP", frame->bp); ui_reg("SI", frame->si); ui_reg("DI", frame->di);
    ui_row_end();
    ui_row();
    ui_reg("CS", frame->cs); ui_reg("IP", frame->ip); ui_reg("FLAGS", frame->flags);
    ui_row_end();
    ui_row();
    ui_reg("DS", frame->ds); ui_reg("ES", frame->es);
    ui_reg("SS", CONTEXT(context)->from_program
                     ? ((user_context_t *)context)->user_ss : KERNEL_DATA_SEG);
    ui_row_end();
    ui_panel_close();

    con_putchar('\n');
    con_print("  ");
    ui_dot(TH_RED, "System halted. ");
    ui_text(TH_SUBTEXT, "Press RESET to restart. ");
    ui_text(TH_OVERLAY, "Look the IP up with: make disasm");
    con_putchar('\n');

    /* The telemetry thread will never run again; send what is buffered */
    telemetry_flush();

    /* Halt forever */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/* ── Handlers called from the ISR stubs ─────── */

uint16_t panic_handler(uint16_t sp)
{
    panic_show(panic_reason, sp);
    return sp;
}

uint16_t exc_divide_handler(uint16_t sp)
{
    panic_show("Divide error (INT 0)", sp);
    return sp;
}

uint16_t exc_step_handler(uint16_t sp)
{
    panic_show("Unexpected single-step trap (INT 1)", sp);
    return sp;
}

uint16_t exc_break_handler(uint16_t sp)
{
    panic_show("Breakpoint (INT 3)", sp);
    return sp;
}

uint16_t exc_overflow_handler(uint16_t sp)
{
    panic_show("Overflow trap (INTO)", sp);
    return sp;
}

/* ── Public API ─────────────────────────────── */

extern void panic_isr(void);
extern void exc_divide_isr(void);
extern void exc_step_isr(void);
extern void exc_break_isr(void);
extern void exc_overflow_isr(void);

void panic_init(void)
{
    idt_install(0x00, exc_divide_isr);
    idt_install(0x01, exc_step_isr);
    idt_install(0x03, exc_break_isr);
    idt_install(0x04, exc_overflow_isr);
    idt_install(PANIC_VECTOR, panic_isr);
}

void kernel_panic(const char *reason)
{
    irq_disable();
    panic_reason = reason;

    /* Capture the registers as they are right now */
    __asm__ __volatile__("int $0x82");

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
