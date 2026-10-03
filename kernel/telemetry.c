/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Telemetry System (protocol v1)
 *
 * Kernel events are appended to a ring buffer as small records. That
 * is all an interrupt handler ever does. A kernel thread wakes ten
 * times a second, turns the buffered records into frames, and sends
 * them over COM1; it also sends the periodic records (counters,
 * memory, per-thread statistics) and feeds bytes received on COM1 to
 * the keyboard buffer, so a dashboard can type into the shell.
 *
 * If the ring is full a record is dropped and counted; the count is
 * reported in the COUNTERS record. Nothing is ever invented.
 *
 * Frame on the wire (docs/telemetry.md):
 *
 *   7E  version type seq tick[4] payload... crc[2]  7E
 *
 * Bytes 7E and 7D between the delimiters are sent as 7D, byte^20.
 * The CRC is CRC-16/CCITT-FALSE over version..payload, low byte first.
 */

#include "telemetry.h"
#include "serial.h"
#include "thread.h"
#include "interrupts.h"
#include "keyboard.h"
#include "memory.h"
#include "kernel.h"
#include "hal.h"

/* ── Configuration ──────────────────────────── */

#define RING_SIZE           4096
#define RECORD_HEADER       6       /* length, type, tick[4] */
#define CONSOLE_CHUNK       48      /* Console characters per record */

/*
 * High-volume records (context switches, state changes, system calls)
 * may only fill the ring this far. The rest is kept for everything
 * else, so a flood of switches cannot crowd out console text or a
 * fault report.
 */
#define RING_BULK_LIMIT     (RING_SIZE * 3 / 4)

#define THREAD_PRIORITY     12      /* Above the shell, so the buffer drains under load */
#define WAKE_PERIOD         (HZ / 10)
#define COUNTERS_EVERY      2       /* wake-ups between COUNTERS records */
#define STATS_EVERY         10      /* wake-ups between HELLO/MEMORY/THREAD_STATS */

/* ── Payload helpers ────────────────────────── */

static uint8_t *put16(uint8_t *p, uint16_t value)
{
    *p++ = (uint8_t)value;
    *p++ = (uint8_t)(value >> 8);
    return p;
}

static uint8_t *put32(uint8_t *p, uint32_t value)
{
    p = put16(p, (uint16_t)value);
    return put16(p, (uint16_t)(value >> 16));
}

/* Copy a thread name into a fixed 12-byte, zero-padded field */
static uint8_t *put_name(uint8_t *p, const char *name)
{
    uint8_t i;

    for (i = 0; i < 12; i++) {
        *p++ = *name ? (uint8_t)*name++ : 0;
    }
    return p;
}

/* ── Ring buffer of records ─────────────────── */

static uint8_t  ring[RING_SIZE];
static uint16_t ring_head;          /* Next byte to write */
static uint16_t ring_tail;          /* Next byte to read */
static uint16_t ring_used;
static uint16_t dropped;            /* Records lost because the ring was full */

/* Console text waiting to become a CONSOLE record */
static uint8_t console_buf[CONSOLE_CHUNK];
static uint8_t console_len;

static uint8_t tx_seq;

static void ring_put(uint8_t byte)
{
    ring[ring_head] = byte;
    ring_head = (ring_head + 1) % RING_SIZE;
}

static uint8_t ring_get(void)
{
    uint8_t byte = ring[ring_tail];

    ring_tail = (ring_tail + 1) % RING_SIZE;
    return byte;
}

/* Append a record; drops it (and counts the drop) if the ring is full */
static void record(uint8_t type, const void *payload, uint8_t len)
{
    const uint8_t *bytes = (const uint8_t *)payload;
    uint16_t flags = hal_irq_save();
    uint8_t header[RECORD_HEADER];
    uint16_t limit = RING_SIZE;
    uint8_t i;

    if (type == TEL_CONTEXT_SWITCH || type == TEL_THREAD_STATE || type == TEL_SYSCALL) {
        limit = RING_BULK_LIMIT;
    }

    /*
     * Build the header in memory first. Passing (uint8_t)(tick >> 8)
     * and friends straight to ring_put() is miscompiled by
     * ia16-elf-gcc 6.3 at -Os (it pushes the wrong stack slots).
     */
    header[0] = len;
    header[1] = type;
    put32(&header[2], tick_count);

    if (ring_used + RECORD_HEADER + len > limit) {
        if (dropped < 0xFFFF) {
            dropped++;
        }
    } else {
        for (i = 0; i < RECORD_HEADER; i++) {
            ring_put(header[i]);
        }
        for (i = 0; i < len; i++) {
            ring_put(bytes[i]);
        }
        ring_used += RECORD_HEADER + len;
    }

    hal_irq_restore(flags);
}

/* Turn pending console text into a record. Interrupts must be off. */
static void console_flush(void)
{
    if (console_len > 0) {
        record(TEL_CONSOLE, console_buf, console_len);
        console_len = 0;
    }
}

/* ── Framing ────────────────────────────────── */

static uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    /* CRC-16/CCITT-FALSE, four bits at a time */
    static const uint16_t table[16] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
        0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
    };

    crc = (crc << 4) ^ table[((crc >> 12) ^ (byte >> 4)) & 0x0F];
    crc = (crc << 4) ^ table[((crc >> 12) ^ byte) & 0x0F];
    return crc;
}

static void send_escaped(uint8_t byte)
{
    if (byte == TEL_FRAME_DELIMITER || byte == TEL_FRAME_ESCAPE) {
        serial_putc(TEL_FRAME_ESCAPE);
        byte ^= TEL_FRAME_ESCAPE_XOR;
    }
    serial_putc(byte);
}

/* tick points at four bytes, low byte first */
static void send_frame(uint8_t type, const uint8_t *tick, const uint8_t *payload, uint8_t len)
{
    uint8_t header[7];
    uint16_t crc = 0xFFFF;
    uint8_t i;

    header[0] = TEL_PROTOCOL_VERSION;
    header[1] = type;
    header[2] = tx_seq++;
    header[3] = tick[0];
    header[4] = tick[1];
    header[5] = tick[2];
    header[6] = tick[3];

    serial_putc(TEL_FRAME_DELIMITER);
    for (i = 0; i < sizeof(header); i++) {
        crc = crc16_update(crc, header[i]);
        send_escaped(header[i]);
    }
    for (i = 0; i < len; i++) {
        crc = crc16_update(crc, payload[i]);
        send_escaped(payload[i]);
    }
    send_escaped((uint8_t)crc);
    send_escaped((uint8_t)(crc >> 8));
    serial_putc(TEL_FRAME_DELIMITER);
}

/* Take one record out of the ring and send it. Returns false if the ring is empty. */
static bool drain_one(void)
{
    uint8_t payload[TEL_MAX_PAYLOAD];
    uint16_t flags = hal_irq_save();
    uint8_t tick[4];
    uint8_t len, type, i;

    if (ring_used == 0) {
        hal_irq_restore(flags);
        return false;
    }

    len  = ring_get();
    type = ring_get();
    for (i = 0; i < sizeof(tick); i++) {
        tick[i] = ring_get();
    }
    for (i = 0; i < len; i++) {
        payload[i] = ring_get();
    }
    ring_used -= RECORD_HEADER + len;

    hal_irq_restore(flags);

    /* The slow part — serial output — runs with interrupts as they were */
    send_frame(type, tick, payload, len);
    return true;
}

/* ── Events ─────────────────────────────────── */

void telemetry_init(void)
{
    ring_head = 0;
    ring_tail = 0;
    ring_used = 0;
    dropped = 0;
    console_len = 0;
    tx_seq = 0;
}

void telemetry_boot_stage(uint8_t stage)
{
    record(TEL_BOOT_STAGE, &stage, 1);
}

void telemetry_thread_created(uint8_t tid)
{
    uint8_t data[14];
    tcb_t *tcb = thread_get_tcb(tid);

    data[0] = tid;
    data[1] = tcb->priority;
    put_name(&data[2], tcb->name);
    record(TEL_THREAD_CREATE, data, sizeof(data));
}

void telemetry_thread_exited(uint8_t tid)
{
    record(TEL_THREAD_EXIT, &tid, 1);
}

void telemetry_thread_state(uint8_t tid)
{
    uint8_t data[3];
    tcb_t *tcb = thread_get_tcb(tid);

    data[0] = tid;
    data[1] = tcb->state;
    data[2] = tcb->priority;
    record(TEL_THREAD_STATE, data, sizeof(data));
}

void telemetry_context_switch(uint8_t from_tid, uint8_t to_tid, uint16_t to_sp)
{
    /* The registers the incoming thread is about to resume with */
    const frame_t *frame = CONTEXT_FRAME(to_sp);
    uint8_t data[24];
    uint8_t *p = data;

    *p++ = from_tid;
    *p++ = to_tid;
    p = put16(p, frame->ip);
    p = put16(p, frame->cs);
    p = put16(p, frame->flags);
    p = put16(p, CONTEXT_RESUME_SP(to_sp));  /* SP after the frame is popped */
    p = put16(p, frame->ax);
    p = put16(p, frame->bx);
    p = put16(p, frame->cx);
    p = put16(p, frame->dx);
    p = put16(p, frame->si);
    p = put16(p, frame->di);
    p = put16(p, frame->bp);
    record(TEL_CONTEXT_SWITCH, data, sizeof(data));
}

void telemetry_syscall(uint8_t tid, uint8_t func)
{
    uint8_t data[2];

    data[0] = tid;
    data[1] = func;
    record(TEL_SYSCALL, data, sizeof(data));
}

void telemetry_fault(uint8_t tid, const frame_t *frame, uint16_t sp, const char *reason)
{
    uint8_t data[TEL_MAX_PAYLOAD];
    uint8_t *p = data;

    *p++ = tid;
    p = put16(p, frame->ip);
    p = put16(p, frame->cs);
    p = put16(p, frame->flags);
    p = put16(p, sp);
    p = put16(p, frame->ax);
    p = put16(p, frame->bx);
    p = put16(p, frame->cx);
    p = put16(p, frame->dx);
    p = put16(p, frame->si);
    p = put16(p, frame->di);
    p = put16(p, frame->bp);
    while (*reason && p < data + sizeof(data)) {
        *p++ = (uint8_t)*reason++;
    }
    record(TEL_FAULT, data, (uint8_t)(p - data));
}

void telemetry_bench(uint8_t kind, uint32_t count)
{
    uint8_t data[5];

    data[0] = kind;
    put32(&data[1], count);
    record(TEL_BENCH, data, sizeof(data));
}

/* Called by the console with interrupts off */
void telemetry_console_char(char c)
{
    console_buf[console_len++] = (uint8_t)c;
    if (c == '\n' || console_len == CONSOLE_CHUNK) {
        console_flush();
    }
}

void telemetry_flush(void)
{
    uint16_t flags = hal_irq_save();

    console_flush();
    while (drain_one()) {
        /* send everything */
    }

    hal_irq_restore(flags);
}

/* ── Periodic records (sent by the telemetry thread) ── */

/* Send a frame stamped with the current tick */
static void send_now(uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t tick[4];

    put32(tick, irq_ticks());
    send_frame(type, tick, payload, len);
}

static void send_hello(void)
{
    uint8_t data[18];
    uint8_t *p = data;
    mem_layout_t layout;

    mem_get_layout(&layout);

    *p++ = TEL_PROTOCOL_VERSION;
    *p++ = HZ;
    *p++ = MAX_THREADS;
    *p++ = 0;                       /* reserved */
    p = put16(p, hal_get_cs());
    p = put16(p, KERNEL_DATA_SEG);
    p = put16(p, layout.far_start);
    p = put16(p, layout.far_end);
    p = put16(p, mem_kb);
    p = put16(p, layout.heap_start);
    p = put16(p, layout.heap_end);
    send_now(TEL_HELLO, data, (uint8_t)(p - data));
}

static void send_counters(void)
{
    uint8_t data[18];
    uint8_t *p = data;
    uint16_t flags = hal_irq_save();

    p = put32(p, irq_timer_count);
    p = put32(p, irq_keyboard_count);
    p = put32(p, irq_syscall_count);
    p = put32(p, context_switch_count);
    p = put16(p, dropped);

    hal_irq_restore(flags);
    send_now(TEL_COUNTERS, data, sizeof(data));
}

static void send_memory(void)
{
    uint8_t data[8];
    uint8_t *p = data;
    mem_layout_t layout;

    mem_get_layout(&layout);

    p = put16(p, mem_free());
    p = put16(p, mem_used());
    p = put16(p, far_free_paras());
    p = put16(p, layout.far_end - layout.far_start);
    send_now(TEL_MEMORY, data, sizeof(data));
}

static void send_thread_stats(void)
{
    uint8_t data[25];
    int tid;

    for (tid = 0; tid < MAX_THREADS; tid++) {
        tcb_t *tcb = thread_get_tcb(tid);
        uint16_t flags = hal_irq_save();
        uint8_t *p = data;

        if (!tcb->active) {
            hal_irq_restore(flags);
            continue;
        }

        *p++ = (uint8_t)tid;
        *p++ = tcb->state;
        *p++ = tcb->priority;
        p = put32(p, tcb->cpu_ticks);
        p = put16(p, tcb->sp);      /* Saved SP; stale for the running thread */
        p = put16(p, tcb->stack_base);
        p = put16(p, tcb->stack_size);
        p = put_name(p, tcb->name);

        hal_irq_restore(flags);
        send_now(TEL_THREAD_STATS, data, sizeof(data));
    }
}

/* ── Telemetry thread ───────────────────────── */

static void telemetry_thread(void)
{
    uint16_t wakeups = 0;
    uint16_t flags;
    uint8_t byte;

    for (;;) {
        /* Dashboard → kernel: received bytes are typed into the keyboard buffer */
        while (serial_getc(&byte)) {
            if (byte == '\r') {
                byte = '\n';
            } else if (byte == 0x7F) {
                byte = '\b';
            }
            kb_inject_char((char)byte);
        }

        /* Text without a newline (a prompt, say) must not wait forever */
        flags = hal_irq_save();
        console_flush();
        hal_irq_restore(flags);

        while (drain_one()) {
            /* send everything buffered */
        }

        if (wakeups % COUNTERS_EVERY == 0) {
            send_counters();
        }
        if (wakeups % STATS_EVERY == 0) {
            send_hello();
            send_memory();
            send_thread_stats();
        }
        wakeups++;

        thread_sleep(WAKE_PERIOD);
    }
}

void telemetry_start(void)
{
    thread_create(telemetry_thread, THREAD_PRIORITY, "telemetry");
}
