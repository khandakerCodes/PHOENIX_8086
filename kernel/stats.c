/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Runtime Statistics Implementation
 *
 * Tracks and displays kernel runtime metrics including
 * CPU usage, context switches, interrupt counts, and uptime.
 */

#include "stats.h"
#include "console.h"
#include "interrupts.h"
#include "thread.h"
#include "memory.h"

uint16_t stats_uptime_seconds(void)
{
    return (uint16_t)(irq_ticks() / HZ);
}

void stats_print(void)
{
    uint16_t uptime = stats_uptime_seconds();
    uint16_t mins = uptime / 60;
    uint16_t secs = uptime % 60;

    con_println("=== Phoenix-8086 Runtime Statistics ===");

    con_print("  Uptime:           ");
    con_print_dec(mins);
    con_print("m ");
    con_print_dec(secs);
    con_println("s");

    con_print("  Timer ticks:      ");
    con_print_dec((uint16_t)tick_count);
    con_putchar('\n');

    con_print("  Context switches: ");
    con_print_dec((uint16_t)context_switch_count);
    con_putchar('\n');

    con_print("  Timer IRQs:       ");
    con_print_dec((uint16_t)irq_timer_count);
    con_putchar('\n');

    con_print("  Keyboard IRQs:    ");
    con_print_dec((uint16_t)irq_keyboard_count);
    con_putchar('\n');

    con_print("  System calls:     ");
    con_print_dec((uint16_t)irq_syscall_count);
    con_putchar('\n');

    con_print("  Active threads:   ");
    con_print_dec(thread_count());
    con_putchar('\n');

    con_print("  Heap free:        ");
    con_print_dec(mem_free());
    con_println(" bytes");

    con_print("  Heap used:        ");
    con_print_dec(mem_used());
    con_println(" bytes");
}
