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

    /* Red background for panic screen */
    con_set_color(VGA_WHITE, VGA_RED);
    con_clear();

    /* Banner */
    con_set_cursor(2, 20);
    con_print("=========================");
    con_set_cursor(3, 20);
    con_print("    KERNEL PANIC         ");
    con_set_cursor(4, 20);
    con_print("=========================");

    /* Reason */
    con_set_cursor(6, 5);
    con_print("Reason: ");
    con_print(reason);

    /* Current thread info */
    con_set_cursor(8, 5);
    con_print("Current Thread: ");
    if (tid >= 0) {
        tcb = thread_get_tcb(tid);
        con_print("TID=");
        con_print_dec(tid);
        if (tcb) {
            con_print("  Name=");
            con_print(tcb->name);
            con_print("  State=");
            con_print_dec(tcb->state);
        }
    } else {
        con_print("None");
    }

    /* Register dump: the frame captured when the panic was raised */
    con_set_cursor(10, 5);
    con_print("Register Dump:");

    con_set_cursor(11, 7);
    con_print("AX=");
    con_print_hex(frame->ax);
    con_print("  BX=");
    con_print_hex(frame->bx);
    con_print("  CX=");
    con_print_hex(frame->cx);
    con_print("  DX=");
    con_print_hex(frame->dx);

    con_set_cursor(12, 7);
    con_print("SP=");
    con_print_hex(CONTEXT_RESUME_SP(context));          /* SP before the interrupt */
    con_print("  BP=");
    con_print_hex(frame->bp);
    con_print("  SI=");
    con_print_hex(frame->si);
    con_print("  DI=");
    con_print_hex(frame->di);

    con_set_cursor(13, 7);
    con_print("CS=");
    con_print_hex(frame->cs);
    con_print("  IP=");
    con_print_hex(frame->ip);
    con_print("  FLAGS=");
    con_print_hex(frame->flags);

    con_set_cursor(14, 7);
    con_print("DS=");
    con_print_hex(frame->ds);
    con_print("  ES=");
    con_print_hex(frame->es);
    con_print("  SS=");
    con_print_hex(CONTEXT(context)->from_program
                      ? ((user_context_t *)context)->user_ss : KERNEL_DATA_SEG);

    /* Interrupt stats */
    con_set_cursor(16, 5);
    con_print("Timer ticks: ");
    con_print_dec((uint16_t)tick_count);
    con_print("  Context switches: ");
    con_print_dec((uint16_t)context_switch_count);

    /* Halt message */
    con_set_cursor(18, 5);
    con_print("System halted. Please reboot.");

    con_set_cursor(20, 5);
    con_set_color(VGA_YELLOW, VGA_RED);
    con_print("Press RESET to restart.\n");

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
