/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Runtime Statistics Implementation
 *
 * Tracks and displays kernel runtime metrics including
 * CPU usage, context switches, interrupt counts, and uptime.
 */

#include "stats.h"
#include "ui.h"
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
    uint16_t up = stats_uptime_seconds();

    ui_panel_open("Runtime Statistics", TH_PEACH, "since boot");
    ui_row();
    ui_text(TH_SUBTEXT, "Uptime:");
    ui_pad_to(UI_TEXT_LEFT + 20);
    ui_num(up / 60, 7, TH_TEXT);
    ui_text(TH_OVERLAY, "m ");
    ui_num(up % 60, 1, TH_TEXT);
    ui_text(TH_OVERLAY, "s");
    ui_pad_to(UI_TEXT_LEFT + 37);
    ui_counter("Active threads:", (uint32_t)thread_count(), NULL);
    ui_row_end();
    ui_row();
    ui_counter("Timer ticks:", tick_count, NULL);
    ui_counter("Context switches:", context_switch_count, NULL);
    ui_row_end();
    ui_row();
    ui_counter("Timer IRQs:", irq_timer_count, NULL);
    ui_counter("Keyboard IRQs:", irq_keyboard_count, NULL);
    ui_row_end();
    ui_row();
    ui_counter("System calls:", irq_syscall_count, NULL);
    ui_counter("Heap free:", mem_free(), "bytes");
    ui_row_end();
    ui_panel_close();
}
