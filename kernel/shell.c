/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Kernel Shell Implementation
 *
 * Interactive command-line shell running as a kernel thread.
 * Provides commands for inspecting threads, memory, interrupts,
 * and triggering demonstrations.
 */

#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "thread.h"
#include "scheduler.h"
#include "memory.h"
#include "interrupts.h"
#include "debug.h"
#include "stats.h"
#include "panic.h"
#include "ipc.h"
#include "syscall.h"
#include "hal.h"
#include "selftest.h"
#include "telemetry.h"
#include "fat12.h"
#include "exec.h"

/* ── Constants ──────────────────────────────── */
#define CMD_BUF_SIZE    64
#define PROMPT          "phoenix> "

/* ── String utilities ───────────────────────── */

static bool str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static int str_to_int(const char *s)
{
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

/* ── Demo thread functions ──────────────────── */

/*
 * Stay CPU-bound for a tenth of a second. Timed by the tick counter
 * rather than a loop count, so the demo looks the same on a 4.77 MHz
 * 8088 and on a fast emulator.
 */
static void demo_spin(void)
{
    uint32_t start = irq_ticks();

    while (irq_ticks() - start < HZ / 10) {
        /* burn CPU */
    }
}

static void demo_thread_a(void)
{
    int i;
    for (i = 0; i < 20; i++) {
        con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        con_print("[A]");
        con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

        demo_spin();
    }
    thread_exit();
}

static void demo_thread_b(void)
{
    int i;
    for (i = 0; i < 20; i++) {
        con_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        con_print("[B]");
        con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

        demo_spin();
    }
    thread_exit();
}

static void demo_thread_c(void)
{
    int i;
    for (i = 0; i < 20; i++) {
        con_set_color(VGA_YELLOW, VGA_BLACK);
        con_print("[C]");
        con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

        demo_spin();
    }
    thread_exit();
}

/* ── IPC demo: producer → mailbox → consumer ── */

#define IPC_DEMO_MESSAGES   5

static mailbox_t demo_mbox;

static void demo_producer(void)
{
    uint16_t i;
    for (i = 1; i <= IPC_DEMO_MESSAGES; i++) {
        mbox_send(&demo_mbox, i);
        thread_sleep(HZ / 10);
    }
}

static void demo_consumer(void)
{
    uint16_t i;
    for (i = 0; i < IPC_DEMO_MESSAGES; i++) {
        uint16_t msg = mbox_recv(&demo_mbox);   /* Blocks until a message arrives */
        con_print("[recv ");
        con_print_dec(msg);
        con_print("]");
    }
    con_println("[ipc done]");
}

/* ── Syscall demo: everything goes through INT 80h ── */

static void demo_syscall(void)
{
    static const char msg_done[] = "[syscall done]\n";
    uint32_t before, after;

    sys_call(SYS_AX(SYS_PUTC, '['), 0, 0, 0);
    sys_call(SYS_AX(SYS_PUTC, 'v'), 0, 0, 0);
    sys_call(SYS_AX(SYS_PUTC, '0' + (uint8_t)sys_call(SYS_AX(SYS_VERSION, 0), 0, 0, 0)), 0, 0, 0);
    sys_call(SYS_AX(SYS_PUTC, ']'), 0, 0, 0);

    before = sys_call(SYS_AX(SYS_TICKS, 0), 0, 0, 0);
    sys_call(SYS_AX(SYS_SLEEP, 0), 0, HZ / 5, 0);
    after = sys_call(SYS_AX(SYS_TICKS, 0), 0, 0, 0);

    con_print("[slept ");
    con_print_dec((uint16_t)(after - before));
    con_print(" ticks]");

    sys_call(SYS_AX(SYS_PUTS, 0), (uint16_t)msg_done, 0, 0);
    sys_call(SYS_AX(SYS_THREAD_EXIT, 0), 0, 0, 0);
}

/* ── Stack overflow demo ────────────────────── */

/*
 * Recurse without end, yielding at every level so the scheduler
 * gets to inspect the stack pointer. It kills the thread when the
 * stack pointer enters the red zone.
 */
static uint16_t demo_recurse(uint16_t depth)
{
    volatile uint8_t pad[32];

    pad[0] = (uint8_t)depth;
    thread_yield();
    return demo_recurse(depth + 1) + pad[0];
}

static void demo_overflow(void)
{
    demo_recurse(0);
}

/* ── Benchmark workers ──────────────────────── */

static volatile bool bench_stop;
static volatile uint32_t bench_yields;

/* Two of these at equal priority hand the CPU back and forth */
static void bench_yielder(void)
{
    while (!bench_stop) {
        bench_yields++;
        thread_yield();
    }
}

/* ── Command handlers ───────────────────────── */

static void cmd_help(void)
{
    con_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    con_println("=== Phoenix-8086 Shell Commands ===");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    con_println("  help       - Show this help");
    con_println("  threads    - List active threads");
    con_println("  ps         - Alias for 'threads'");
    con_println("  memory     - Show memory map");
    con_println("  ticks      - Show tick counter");
    con_println("  uptime     - Show system uptime");
    con_println("  stats      - Show runtime statistics");
    con_println("  interrupts - Show interrupt counters");
    con_println("  registers  - Dump CPU registers");
    con_println("  scheduler  - Show scheduler queue");
    con_println("  clear      - Clear screen");
    con_println("  create     - Create a demo thread");
    con_println("  ipc        - Mailbox producer/consumer demo");
    con_println("  syscall    - INT 80h system call demo");
    con_println("  kill <tid> - Kill a thread");
    con_println("  nice <tid> <pri> - Set thread priority");
    con_println("  sleep <ticks>    - Put the shell to sleep");
    con_println("  bench      - Measure context switch and heap cost");
    con_println("  selftest   - Run kernel self-tests");
    con_println("  overflow   - Stack overflow detection demo");
    con_println("  divzero    - Trigger a divide error");
    con_println("  panic      - Trigger kernel panic");
    con_println("  reboot     - Reboot the system");
    con_println("  ls         - List files on the boot disk");
    con_println("  cat <file> - Print a text file");
    con_println("  run <file> - Load and run a program");
    con_println("  keymap [name]    - Show or set the keyboard layout");
    con_println("  cpu        - Identify the processor");
    con_println("  about      - About Phoenix-8086");
}

static void cmd_about(void)
{
    con_set_color(VGA_LIGHT_RED, VGA_BLACK);
    con_println("");
    con_println("  ____  _                      _         ___   ___   ___   __");
    con_println(" |  _ \\| |__   ___   ___ _ __ (_)_  __  ( _ ) / _ \\ ( _ ) / /_");
    con_println(" | |_) | '_ \\ / _ \\ / _ \\ '_ \\| \\ \\/ /  / _ \\| | | |/ _ \\| '_ \\");
    con_println(" |  __/| | | | (_) |  __/ | | | |>  <  | (_) | |_| | (_) | (_) |");
    con_println(" |_|   |_| |_|\\___/ \\___|_| |_|_/_/\\_\\  \\___/ \\___/ \\___/ \\___/");
    con_println("");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    con_println("  A Bare-Metal Preemptive Microkernel for the Intel 8086");
    con_println("  Designed for educational demonstration and systems study");
    con_println("");
    con_print("  Threads: ");
    con_print_dec(thread_count());
    con_print("  |  Uptime: ");
    con_print_dec(stats_uptime_seconds());
    con_print("s  |  Ticks: ");
    con_print_dec((uint16_t)irq_ticks());
    con_println("");
}

static void report_created(int tid)
{
    if (tid >= 0) {
        con_print("Created thread TID=");
        con_print_dec(tid);
        con_putchar('\n');
    } else {
        con_println("Error: No free thread slots");
    }
}

static void cmd_ipc(void)
{
    mbox_init(&demo_mbox);
    report_created(thread_create(demo_consumer, 6, "consumer"));
    report_created(thread_create(demo_producer, 6, "producer"));
}

static void cmd_syscall(void)
{
    report_created(thread_create(demo_syscall, 6, "syscall"));
}

static void cmd_create(void)
{
    static int demo_count = 0;
    int tid;

    switch (demo_count % 3) {
    case 0:
        tid = thread_create(demo_thread_a, 5, "demo-A");
        break;
    case 1:
        tid = thread_create(demo_thread_b, 3, "demo-B");
        break;
    default:
        tid = thread_create(demo_thread_c, 4, "demo-C");
        break;
    }

    report_created(tid);

    demo_count++;
}

static void cmd_nice(const char *arg)
{
    int tid = str_to_int(arg);
    int pri;

    /* Second argument follows the first number */
    while (*arg >= '0' && *arg <= '9') arg++;
    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        con_println("Usage: nice <tid> <priority>");
        return;
    }
    pri = str_to_int(arg);

    if (pri > 255 || !thread_set_priority(tid, (uint8_t)pri)) {
        con_println("nice: no such thread or bad priority");
        return;
    }
    con_print("Thread ");
    con_print_dec(tid);
    con_print(" priority set to ");
    con_print_dec(pri);
    con_putchar('\n');
}

static void cmd_sleep(const char *arg)
{
    uint32_t start = irq_ticks();

    thread_sleep((uint16_t)str_to_int(arg));
    con_print("Slept ");
    con_print_dec((uint16_t)(irq_ticks() - start));
    con_println(" ticks");
}

/* Print an unsigned 32-bit integer in decimal */
static void print_u32(uint32_t value)
{
    char buf[10];
    int i = 0;

    do {
        buf[i++] = '0' + (char)(value % 10);
        value /= 10;
    } while (value > 0);

    while (i > 0) {
        con_putchar(buf[--i]);
    }
}

/* Print "<count> <what>/sec, <n> us each" */
static void bench_report(uint8_t kind, uint32_t count, const char *what)
{
    uint32_t us = count ? 1000000UL / count : 0;

    telemetry_bench(kind, count);

    con_print("bench: ");
    print_u32(count);
    con_putchar(' ');
    con_print(what);
    con_print("/sec, ");
    if (count && us == 0) {
        con_print("under 1");
    } else {
        print_u32(us);
    }
    con_println(" us each");
}

static void cmd_bench(void)
{
    uint32_t count;
    uint32_t start;
    void *block;

    /* Context switches: two threads yielding to each other for one second */
    bench_stop = false;
    bench_yields = 0;
    if (thread_create(bench_yielder, 8, "bench-1") < 0 ||
        thread_create(bench_yielder, 8, "bench-2") < 0) {
        bench_stop = true;
        con_println("bench: no free thread slots");
        return;
    }
    thread_sleep(HZ);
    bench_stop = true;
    thread_sleep(2);        /* Let both workers see the flag and exit */
    count = bench_yields;

    bench_report(TEL_BENCH_SWITCHES, count, "context switches");

    /* Heap: allocate/free pairs in half a second */
    count = 0;
    start = irq_ticks();
    while (irq_ticks() - start < HZ / 2) {
        block = kmalloc(64);
        kfree(block);
        count++;
    }
    count *= 2;
    bench_report(TEL_BENCH_HEAP, count, "kmalloc+kfree pairs");
}

static void cmd_ls(void)
{
    fat_dirent_t entry;
    uint16_t index = 0;
    uint16_t count = 0;

    if (!fat_mounted()) {
        con_println("No file system mounted");
        return;
    }

    while (fat_next(&index, &entry)) {
        int len = 0;

        con_print("  ");
        con_print(entry.name);
        while (entry.name[len]) len++;
        while (len++ < 14) con_putchar(' ');
        print_u32(entry.size);
        con_println(" bytes");
        count++;
    }
    con_print_dec(count);
    con_println(" file(s)");
}

static void cmd_cat(const char *name)
{
    static uint8_t buffer[64];
    fat_file_t file;
    uint16_t got, i;

    if (name[0] == '\0') {
        con_println("Usage: cat <file>");
        return;
    }
    if (!fat_open(name, &file)) {
        con_print("File not found: ");
        con_println(name);
        return;
    }

    while ((got = fat_read(&file, buffer, sizeof(buffer))) > 0) {
        for (i = 0; i < got; i++) {
            if (buffer[i] != '\r') {
                con_putchar((char)buffer[i]);
            }
        }
    }
}

static void cmd_run(const char *name)
{
    uint8_t error;
    int tid;

    if (name[0] == '\0') {
        con_println("Usage: run <file>");
        return;
    }

    tid = exec_program(name, &error);
    if (tid < 0) {
        con_print("Cannot run ");
        con_print(name);
        con_print(": ");
        con_println(exec_error_text(error));
        return;
    }
    con_print("Started ");
    con_print(name);
    con_print(" as TID=");
    con_print_dec(tid);
    con_putchar('\n');
}

static void cmd_keymap(const char *name)
{
    const char *layout, *description;
    uint8_t i;

    if (name[0] != '\0') {
        if (!kb_set_keymap(name)) {
            con_print("Unknown keymap: ");
            con_println(name);
            return;
        }
        con_print("Keymap: ");
        con_println(kb_keymap_name());
        return;
    }

    for (i = 0; kb_keymap_info(i, &layout, &description); i++) {
        con_print(str_eq(layout, kb_keymap_name()) ? "* " : "  ");
        con_print(layout);
        con_print("  ");
        con_println(description);
    }
}

static void cmd_cpu(void)
{
    static const char *const names[] = {
        "8086/8088", "80186 or V20/V30", "80286 or later"
    };

    con_print("CPU: ");
    con_println(names[hal_cpu_class()]);
}

static void cmd_divzero(void)
{
    volatile int zero = 0;
    volatile int result = 1 / zero;

    (void)result;
}

static void cmd_kill(const char *arg)
{
    int tid;

    if (arg[0] == '\0') {
        con_println("Usage: kill <tid>");
        return;
    }

    tid = str_to_int(arg);
    if (tid == 0) {
        con_println("Cannot kill idle thread");
        return;
    }

    tcb_t *tcb = thread_get_tcb(tid);
    if (!tcb || !tcb->active) {
        con_print("Thread ");
        con_print_dec(tid);
        con_println(" not found");
        return;
    }

    con_print("Killing thread ");
    con_print_dec(tid);
    con_print(" (");
    con_print(tcb->name);
    con_println(")");
    thread_destroy(tid);
}

static void cmd_reboot(void)
{
    con_println("Rebooting...");

    /* Jump to BIOS reset vector at FFFF:0000 */
    __asm__ __volatile__(
        "cli\n\t"
        "ljmp $0xFFFF, $0x0000\n\t"
    );
}

/* ── Shell main loop ────────────────────────── */

static void process_command(char *cmd)
{
    char *arg = cmd;

    /* Skip to first space to find argument */
    while (*arg && *arg != ' ') arg++;
    if (*arg == ' ') {
        *arg = '\0';
        arg++;
        /* Skip leading spaces in argument */
        while (*arg == ' ') arg++;
    }

    if (str_eq(cmd, "help")) {
        cmd_help();
    } else if (str_eq(cmd, "threads") || str_eq(cmd, "ps")) {
        debug_thread_list();
    } else if (str_eq(cmd, "memory")) {
        mem_print_map();
    } else if (str_eq(cmd, "ticks")) {
        con_print("Ticks: ");
        con_print_dec((uint16_t)irq_ticks());
        con_putchar('\n');
    } else if (str_eq(cmd, "uptime")) {
        uint16_t up = stats_uptime_seconds();
        con_print("Uptime: ");
        con_print_dec(up / 60);
        con_print("m ");
        con_print_dec(up % 60);
        con_println("s");
    } else if (str_eq(cmd, "stats")) {
        stats_print();
    } else if (str_eq(cmd, "interrupts")) {
        debug_irq_counts();
    } else if (str_eq(cmd, "registers")) {
        debug_dump_regs();
    } else if (str_eq(cmd, "scheduler")) {
        debug_sched_queue();
    } else if (str_eq(cmd, "clear")) {
        con_clear();
    } else if (str_eq(cmd, "create")) {
        cmd_create();
    } else if (str_eq(cmd, "ipc")) {
        cmd_ipc();
    } else if (str_eq(cmd, "syscall")) {
        cmd_syscall();
    } else if (str_eq(cmd, "nice")) {
        cmd_nice(arg);
    } else if (str_eq(cmd, "sleep")) {
        cmd_sleep(arg);
    } else if (str_eq(cmd, "bench")) {
        cmd_bench();
    } else if (str_eq(cmd, "selftest")) {
        selftest_run();
    } else if (str_eq(cmd, "overflow")) {
        report_created(thread_create(demo_overflow, 6, "overflow"));
    } else if (str_eq(cmd, "ls")) {
        cmd_ls();
    } else if (str_eq(cmd, "cat")) {
        cmd_cat(arg);
    } else if (str_eq(cmd, "run")) {
        cmd_run(arg);
    } else if (str_eq(cmd, "keymap")) {
        cmd_keymap(arg);
    } else if (str_eq(cmd, "cpu")) {
        cmd_cpu();
    } else if (str_eq(cmd, "divzero")) {
        cmd_divzero();
    } else if (str_eq(cmd, "kill")) {
        cmd_kill(arg);
    } else if (str_eq(cmd, "panic")) {
        kernel_panic("User-triggered panic via shell");
    } else if (str_eq(cmd, "reboot")) {
        cmd_reboot();
    } else if (str_eq(cmd, "about")) {
        cmd_about();
    } else if (cmd[0] != '\0') {
        con_print("Unknown command: ");
        con_println(cmd);
        con_println("Type 'help' for available commands.");
    }
}

void shell_run(void)
{
    char cmd_buf[CMD_BUF_SIZE];
    int pos;
    char c;

    /* Print welcome banner */
    con_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    con_println("");
    con_println("Phoenix-8086 Kernel Shell v1.0");
    con_println("Type 'help' for available commands.");
    con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    con_println("");

    for (;;) {
        /* Print prompt */
        con_set_color(VGA_LIGHT_BLUE, VGA_BLACK);
        con_print(PROMPT);
        con_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

        /* Read command line */
        pos = 0;
        while (true) {
            c = kb_getchar();

            if (c == '\n') {
                con_putchar('\n');
                cmd_buf[pos] = '\0';
                break;
            } else if (c == '\b') {
                if (pos > 0) {
                    pos--;
                    con_putchar('\b');
                }
            } else if (pos < CMD_BUF_SIZE - 1) {
                cmd_buf[pos++] = c;
                con_putchar(c);
            }
        }

        /* Process the command */
        if (pos > 0) {
            process_command(cmd_buf);
        }
    }
}
