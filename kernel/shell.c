/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Kernel Shell Implementation
 *
 * Interactive command-line shell running as a kernel thread.
 * Provides commands for inspecting threads, memory, interrupts,
 * and triggering demonstrations.
 */

#include "shell.h"
#include "kernel.h"
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
#include "disk.h"
#include "ui.h"

/* ── Constants ──────────────────────────────── */
#define CMD_BUF_SIZE    64

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
        con_set_color(TH_GREEN, TH_BASE);
        con_print("[A]");
        con_set_color(TH_TEXT, TH_BASE);

        demo_spin();
    }
    thread_exit();
}

static void demo_thread_b(void)
{
    int i;
    for (i = 0; i < 20; i++) {
        con_set_color(TH_SKY, TH_BASE);
        con_print("[B]");
        con_set_color(TH_TEXT, TH_BASE);

        demo_spin();
    }
    thread_exit();
}

static void demo_thread_c(void)
{
    int i;
    for (i = 0; i < 20; i++) {
        con_set_color(TH_YELLOW, TH_BASE);
        con_print("[C]");
        con_set_color(TH_TEXT, TH_BASE);

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

/*
 * The help screen: two columns of grouped commands. A line with no
 * description is a group heading.
 */
typedef struct {
    const char *name;
    const char *what;
} help_line_t;

static const help_line_t help_left[] = {
    { "THREADS", NULL },
    { "ps  threads", "list threads" },
    { "stacks", "deepest stack use" },
    { "create", "start a demo thread" },
    { "kill <tid>", "stop a thread" },
    { "nice <tid> <p>", "change a priority" },
    { "sleep <ticks>", "pause the shell" },
    { "", "" },
    { "LOOK INSIDE", NULL },
    { "memory", "memory map" },
    { "stats", "runtime statistics" },
    { "interrupts", "interrupt counters" },
    { "registers", "processor registers" },
    { "scheduler", "who runs next" },
    { "ticks  uptime", "time since boot" },
    { "cpu  about", "processor, version" },
};

static const help_line_t help_right[] = {
    { "FILES AND PROGRAMS", NULL },
    { "ls", "list the boot disk" },
    { "cat <file>", "print a text file" },
    { "run <file>", "run a program" },
    { "disk <driver>", "native or bios" },
    { "", "" },
    { "DEMOS", NULL },
    { "ipc  syscall", "messages, INT 80h" },
    { "bench  selftest", "measure, self-check" },
    { "overflow", "catch a runaway" },
    { "", "" },
    { "SYSTEM", NULL },
    { "keymap [name]", "keyboard layout" },
    { "clear  reboot", "screen, restart" },
    { "panic  divzero", "halt on purpose" },
    { "", "" },
};

#define HELP_RIGHT_COL  41
#define HELP_WHAT_GAP   16

static void help_cell(const help_line_t *line, uint8_t col)
{
    ui_pad_to(col);
    if (line->what == NULL) {
        ui_text(TH_TEAL, line->name);
    } else if (line->name[0] != '\0') {
        ui_text(TH_MAUVE, line->name);
        con_putchar(' ');
        ui_pad_to((uint8_t)(col + HELP_WHAT_GAP));
        ui_text(TH_SUBTEXT, line->what);
    }
}

static void cmd_help(void)
{
    uint8_t i;

    ui_panel_open("Shell Commands", TH_MAUVE, "Phoenix-8086 " PHOENIX_VERSION);
    for (i = 0; i < sizeof(help_left) / sizeof(help_left[0]); i++) {
        ui_row();
        help_cell(&help_left[i], UI_TEXT_LEFT);
        help_cell(&help_right[i], HELP_RIGHT_COL);
        ui_row_end();
    }
    ui_panel_close();
}

static void cmd_about(void)
{
    con_putchar('\n');
    ui_logo(5);
    con_putchar('\n');
    ui_panel_open("About", TH_MAUVE, NULL);
    ui_row();
    ui_kv("Version:", 12, TH_TEXT, PHOENIX_VERSION);
    ui_row_end();
    ui_row();
    ui_kv("Kernel:", 12, TH_TEXT, "a preemptive teaching kernel for the Intel 8086");
    ui_row_end();
    ui_row();
    ui_text(TH_SUBTEXT, "Threads: ");
    ui_pad_to(UI_TEXT_LEFT + 12);
    ui_num((uint32_t)thread_count(), 1, TH_TEXT);
    ui_row_end();
    ui_row();
    ui_text(TH_SUBTEXT, "Uptime: ");
    ui_pad_to(UI_TEXT_LEFT + 12);
    ui_num(stats_uptime_seconds(), 1, TH_TEXT);
    ui_text(TH_TEXT, "s");
    ui_row_end();
    ui_row();
    ui_kv("Source:", 12, TH_BLUE, "github.com/memset2020/PHOENIX_8086");
    ui_row_end();
    ui_panel_close();
}

/* Start a result line: "  ✓ " or "  ✗ " */
static void result(bool ok)
{
    con_print("  ");
    ui_mark(ok);
}

/* Start an information line: "  ● " */
static void info(void)
{
    con_print("  ");
    ui_dot(TH_BLUE, "");
}

static void report_created(int tid)
{
    result(tid >= 0);
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
        result(false);
        con_println("Usage: nice <tid> <priority>");
        return;
    }
    pri = str_to_int(arg);

    if (pri > 255 || !thread_set_priority(tid, (uint8_t)pri)) {
        result(false);
        con_println("nice: no such thread or bad priority");
        return;
    }
    result(true);
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
    result(true);
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

/* PIT counts to nanoseconds: one count is 838.1 ns */
static uint32_t counts_to_ns(uint16_t counts)
{
    return (uint32_t)counts * 8381UL / 10UL;
}

/* Print "<count> <what>/sec, <n> us each" */
static void bench_report(uint8_t kind, uint32_t count, const char *what)
{
    uint32_t us = count ? 1000000UL / count : 0;

    telemetry_bench(kind, count);

    info();
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
        result(false);
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

    /* Timer interrupt latency over half a second (the shell sleeps; idle halts) */
    {
        uint16_t min, average, max, samples;

        timer_latency_start();
        thread_sleep(HZ / 2);
        samples = timer_latency_stop(&min, &average, &max);

        telemetry_bench(TEL_BENCH_IRQ_AVG_NS, counts_to_ns(average));
        telemetry_bench(TEL_BENCH_IRQ_MAX_NS, counts_to_ns(max));
        info();
        con_print("bench: timer interrupt latency ");
        print_u32(counts_to_ns(min) / 1000);
        con_print("-");
        print_u32(counts_to_ns(max) / 1000);
        con_print(" us, average ");
        print_u32(counts_to_ns(average) / 1000);
        con_print(" us (");
        con_print_dec(samples);
        con_println(" ticks)");
    }
}

/* True if a file name ends in .BIN, the extension programs use */
static bool is_program(const char *name)
{
    while (*name && *name != '.') name++;
    return str_eq(name, ".BIN");
}

static void cmd_ls(void)
{
    fat_dirent_t entry;
    uint16_t index = 0;
    uint16_t count = 0;
    uint32_t total = 0;

    if (!fat_mounted()) {
        result(false);
        con_println("No file system mounted");
        return;
    }

    ui_panel_open("Boot Disk", TH_BLUE, "FAT12, root directory");
    while (fat_next(&index, &entry)) {
        bool program = is_program(entry.name);

        ui_row();
        ui_dot(program ? TH_MAUVE : TH_TEAL, "");
        ui_text(program ? TH_TEXT : TH_SUBTEXT, entry.name);
        ui_pad_to(20);
        ui_num(entry.size, 7, TH_TEXT);
        ui_text(TH_OVERLAY, " bytes");
        ui_pad_to(36);
        ui_text(TH_OVERLAY, program ? "program, try: run " : "text, try: cat ");
        ui_text(TH_OVERLAY, entry.name);
        ui_row_end();
        count++;
        total += entry.size;
    }
    ui_row_rule();
    ui_row();
    ui_num(count, 1, TH_TEXT);
    ui_text(TH_SUBTEXT, " file(s), ");
    ui_num(total, 1, TH_TEXT);
    ui_text(TH_SUBTEXT, " bytes");
    ui_row_end();
    ui_panel_close();
}

static void cmd_cat(const char *name)
{
    static uint8_t buffer[64];
    fat_file_t file;
    uint16_t got, i;

    if (name[0] == '\0') {
        result(false);
        con_println("Usage: cat <file>");
        return;
    }
    if (!fat_open(name, &file)) {
        result(false);
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
        result(false);
        con_println("Usage: run <file>");
        return;
    }

    tid = exec_program(name, &error);
    if (tid < 0) {
        result(false);
        con_print("Cannot run ");
        con_print(name);
        con_print(": ");
        con_println(exec_error_text(error));
        return;
    }
    result(true);
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
            result(false);
            con_print("Unknown keymap: ");
            con_println(name);
            return;
        }
        result(true);
        con_print("Keymap: ");
        con_println(kb_keymap_name());
        return;
    }

    for (i = 0; kb_keymap_info(i, &layout, &description); i++) {
        bool current = str_eq(layout, kb_keymap_name());

        con_print("  ");
        ui_dot(current ? TH_GREEN : TH_SURFACE2, "");
        ui_text(current ? TH_TEXT : TH_SUBTEXT, layout);
        ui_pad_to(10);
        ui_text(TH_SUBTEXT, description);
        if (current) {
            ui_text(TH_GREEN, "  (in use)");
        }
        con_putchar('\n');
    }
}

static void cmd_disk(const char *arg)
{
    if (str_eq(arg, "native") || str_eq(arg, "bios")) {
        if (!disk_select(str_eq(arg, "native") ? DISK_NATIVE : DISK_BIOS)) {
            result(false);
            con_println("No floppy controller answered; still using the BIOS");
        }
    } else if (arg[0] != '\0') {
        result(false);
        con_println("Usage: disk [native|bios]");
        return;
    }
    info();
    con_print("Disk: ");
    con_println(disk_driver_name());
}

static void cmd_cpu(void)
{
    static const char *const names[] = {
        "8086/8088", "80186 or V20/V30", "80286 or later"
    };

    info();
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
        result(false);
        con_println("Usage: kill <tid>");
        return;
    }

    tid = str_to_int(arg);
    if (tid == 0) {
        result(false);
        con_println("Cannot kill idle thread");
        return;
    }

    tcb_t *tcb = thread_get_tcb(tid);
    if (!tcb || !tcb->active) {
        result(false);
        con_print("Thread ");
        con_print_dec(tid);
        con_println(" not found");
        return;
    }

    result(true);
    con_print("Killing thread ");
    con_print_dec(tid);
    con_print(" (");
    con_print(tcb->name);
    con_println(")");
    thread_destroy(tid);
}

static void cmd_reboot(void)
{
    info();
    con_println("Rebooting...");
    telemetry_flush();      /* the telemetry thread will not get another turn */

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
    } else if (str_eq(cmd, "stacks")) {
        debug_stack_list();
    } else if (str_eq(cmd, "memory")) {
        mem_print_map();
    } else if (str_eq(cmd, "ticks")) {
        info();
        con_print("Ticks: ");
        con_print_dec((uint16_t)irq_ticks());
        con_putchar('\n');
    } else if (str_eq(cmd, "uptime")) {
        uint16_t up = stats_uptime_seconds();
        info();
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
    } else if (str_eq(cmd, "disk")) {
        cmd_disk(arg);
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
        result(false);
        con_print("Unknown command: ");
        con_println(cmd);
        con_print("    ");
        ui_text(TH_SUBTEXT, "Type ");
        ui_text(TH_MAUVE, "help");
        ui_text(TH_SUBTEXT, " for the list.");
        con_putchar('\n');
    }
}

void shell_run(void)
{
    char cmd_buf[CMD_BUF_SIZE];
    int pos;
    char c;

    /* Welcome line */
    con_print("  ");
    ui_text(TH_SUBTEXT, "Type ");
    ui_text(TH_MAUVE, "help");
    ui_text(TH_SUBTEXT, " for the commands, or try ");
    ui_text(TH_MAUVE, "ps");
    ui_text(TH_SUBTEXT, ", ");
    ui_text(TH_MAUVE, "stacks");
    ui_text(TH_SUBTEXT, " and ");
    ui_text(TH_MAUVE, "run hello.bin");
    ui_text(TH_SUBTEXT, ".");
    con_println("");
    con_println("");

    for (;;) {
        /* A blank line before each prompt unless the screen is already at a fresh line */
        if (con_get_col() != 0) {
            con_putchar('\n');
        }
        ui_prompt();

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
