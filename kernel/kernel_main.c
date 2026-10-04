/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Kernel Main
 *
 * The C entry point called from entry.asm after segment
 * and BSS initialization. This is where the kernel comes
 * alive: console, interrupts, memory, scheduler, and
 * finally the interactive shell.
 */

#include "kernel.h"
#include "console.h"
#include "interrupts.h"
#include "keyboard.h"
#include "memory.h"
#include "scheduler.h"
#include "thread.h"
#include "shell.h"
#include "stats.h"
#include "panic.h"
#include "serial.h"
#include "hal.h"
#include "fat12.h"
#include "disk.h"
#include "keyboard.h"
#include "ui.h"

/* Boot drive and memory info from entry.asm */
extern uint8_t  boot_drive;
extern uint16_t mem_kb;

/* Idle loop (idle.c) */
extern void idle_thread(void);

/*
 * kernel_main — Kernel initialization sequence
 *
 * This follows the milestone progression:
 *   1. Console init (Milestone 2)
 *   2. Memory init (Milestone 2)
 *   3. Interrupt init (Milestone 3)
 *   4. Keyboard init (Milestone 4)
 *   5. Scheduler init (Milestone 3)
 *   6. Shell launch (Milestone 4)
 */
void kernel_main(void)
{
    uint16_t boot_flags;

    /* Serial port and telemetry buffer first, so early events are captured */
    serial_init();
    telemetry_init();
    telemetry_boot_stage(1);

    /* ── Step 1: Console ────────────────────── */
    con_init();
    telemetry_boot_stage(2);

    /* CPU exception traps and the panic vector, before anything can fail */
    panic_init();

    /* Boot banner */
    con_putchar('\n');
    ui_logo(3);
    con_putchar('\n');
    con_print("   ");
    ui_text(TH_MAUVE, "Phoenix-8086 ");
    ui_text(TH_TEXT, PHOENIX_VERSION);
    ui_text(TH_OVERLAY, "  \xFA  ");
    ui_text(TH_SUBTEXT, "a preemptive kernel for the Intel 8086");
    con_putchar('\n');
    con_putchar('\n');

    ui_step("Boot drive");
    con_print("  ");
    con_print_hex(boot_drive);
    ui_text(TH_OVERLAY, ", ");
    con_print_dec(mem_kb);
    con_println(" KB of memory");

    /* ── Step 2: Memory Manager ─────────────── */
    ui_step("Memory manager");
    mem_init();
    telemetry_boot_stage(3);
    ui_mark(true);
    con_print_dec(mem_free());
    con_println(" bytes of near heap");

    /* ── Step 3: Keyboard Driver ────────────── */
    ui_step("Keyboard");
    kb_init();
    ui_mark(true);
    con_print(kb_keymap_name());
    con_println(" layout");

    /* ── Step 4: Scheduler ──────────────────── */
    ui_step("Scheduler");
    sched_init();
    telemetry_boot_stage(4);
    ui_mark(true);
    con_println("priorities, time slices, aging");

    /* ── Step 5: Interrupt System ───────────── */
    ui_step("Interrupts");
    irq_init();
    telemetry_boot_stage(5);
    ui_mark(true);
    con_println("timer 100 Hz, keyboard, INT 80h");

    /* ── Disk and file system (need interrupts to be running) ── */
    ui_step("Disk");
    disk_init();
    ui_mark(true);
    con_println(disk_driver_name());

    ui_step("File system");
    if (fat_mount()) {
        ui_mark(true);
        con_println("FAT12 mounted");
    } else {
        ui_mark(false);
        con_println("no FAT12 volume");
    }

    /*
     * Hold interrupts off until the boot log is finished, so the new
     * threads cannot start printing in the middle of it.
     */
    boot_flags = hal_irq_save();

    /* ── Step 6: Create shell thread ────────── */
    ui_step("Shell");
    int shell_tid = thread_create(shell_run, 10, "shell");
    telemetry_boot_stage(6);
    ui_mark(shell_tid >= 0);
    if (shell_tid >= 0) {
        con_print("thread ");
        con_print_dec(shell_tid);
        con_putchar('\n');
    } else {
        con_println("no free thread slot");
    }

    /* ── Step 7: Telemetry thread ───────────── */
    telemetry_start();

    /* ── Boot complete ──────────────────────── */
    telemetry_boot_stage(7);
    con_putchar('\n');
    ui_ok("Phoenix-8086 boot complete. All subsystems initialized.");
    con_putchar('\n');

    ui_status_start();
    hal_irq_restore(boot_flags);

    /*
     * The shell is now a scheduled thread. This boot context was
     * adopted as thread 0 by sched_init(), so it simply becomes the
     * idle loop: it runs whenever no other thread is READY.
     */
    idle_thread();
}
