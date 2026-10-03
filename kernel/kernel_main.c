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
    con_set_color(VGA_LIGHT_RED, VGA_BLACK);
    con_println("  ____  _                      _");
    con_println(" |  _ \\| |__   ___   ___ _ __ (_)_  __");
    con_println(" | |_) | '_ \\ / _ \\ / _ \\ '_ \\| \\ \\/ /");
    con_println(" |  __/| | | | (_) |  __/ | | | |>  <");
    con_println(" |_|   |_| |_|\\___/ \\___|_| |_|_/_/\\_\\");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    con_println("");

    con_set_color(VGA_WHITE, VGA_BLACK);
    con_println("Phoenix-8086 Microkernel v0.1");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    con_println("A Bare-Metal Preemptive OS for the Intel 8086");
    con_println("============================================");
    con_println("");

    /* Boot info */
    con_print("[BOOT] Drive: ");
    con_print_hex(boot_drive);
    con_print("  Memory: ");
    con_print_dec(mem_kb);
    con_println(" KB");

    /* ── Step 2: Memory Manager ─────────────── */
    con_print("[INIT] Memory manager... ");
    mem_init();
    telemetry_boot_stage(3);
    con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    con_println("OK");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    con_print("       Heap free: ");
    con_print_dec(mem_free());
    con_println(" bytes");

    /* ── Step 3: Keyboard Driver ────────────── */
    con_print("[INIT] Keyboard driver... ");
    kb_init();
    con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    con_println("OK");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    /* ── Step 4: Scheduler ──────────────────── */
    con_print("[INIT] Scheduler... ");
    sched_init();
    telemetry_boot_stage(4);
    con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    con_println("OK");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    /* ── Step 5: Interrupt System ───────────── */
    con_print("[INIT] Interrupt system... ");
    irq_init();
    telemetry_boot_stage(5);
    con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    con_println("OK");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    con_println("[INIT] IRQ0 (Timer)    -> Installed");
    con_println("[INIT] IRQ1 (Keyboard) -> Installed");
    con_println("[INIT] INT 80h (Syscall) -> Installed");

    /*
     * Hold interrupts off until the boot log is finished, so the new
     * threads cannot start printing in the middle of it.
     */
    boot_flags = hal_irq_save();

    /* ── Step 6: Create shell thread ────────── */
    con_print("[INIT] Creating shell thread... ");
    int shell_tid = thread_create(shell_run, 10, "shell");
    telemetry_boot_stage(6);
    if (shell_tid >= 0) {
        con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        con_print("OK (TID=");
        con_print_dec(shell_tid);
        con_println(")");
    } else {
        con_set_color(VGA_LIGHT_RED, VGA_BLACK);
        con_println("FAILED");
    }
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    /* ── Step 7: Telemetry thread ───────────── */
    telemetry_start();

    /* ── Boot complete ──────────────────────── */
    telemetry_boot_stage(7);
    con_println("");
    con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    con_println("============================================");
    con_println("  Phoenix-8086 boot complete.");
    con_println("  All subsystems initialized.");
    con_println("============================================");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    con_println("");

    hal_irq_restore(boot_flags);

    /*
     * The shell is now a scheduled thread. This boot context was
     * adopted as thread 0 by sched_init(), so it simply becomes the
     * idle loop: it runs whenever no other thread is READY.
     */
    idle_thread();
}
