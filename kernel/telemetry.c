/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Telemetry System
 *
 * Serializes kernel state into structured telemetry packets
 * and emits them over the virtual serial port (COM1, 0x3F8).
 * The companion dashboard receives these via a serial-to-WebSocket
 * bridge and renders them as live visualizations.
 *
 * Packet format:
 *   [START_BYTE (0xFE)] [TYPE] [LENGTH] [DATA...] [CHECKSUM]
 */

#include "../include/types.h"
#include "console.h"
#include "interrupts.h"
#include "kernel.h"
#include "hal.h"

/* ── Serial port I/O ────────────────────────── */

#define SERIAL_PORT     0x3F8   /* COM1 */
#define SERIAL_BAUD     115200
#define SERIAL_DIVISOR   1      /* 115200 / 115200 */

/* ── Serial port initialization ─────────────── */

void telemetry_init(void)
{
    /* Disable all interrupts on the serial port */
    outb(SERIAL_PORT + 1, 0x00);

    /* Set baud rate divisor */
    outb(SERIAL_PORT + 3, 0x80);  /* Enable DLAB */
    outb(SERIAL_PORT + 0, SERIAL_DIVISOR & 0xFF);  /* Low byte */
    outb(SERIAL_PORT + 1, (SERIAL_DIVISOR >> 8) & 0xFF);  /* High byte */

    /* 8 bits, no parity, 1 stop bit */
    outb(SERIAL_PORT + 3, 0x03);

    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(SERIAL_PORT + 2, 0xC7);

    /* Enable IRQs, RTS/DSR set */
    outb(SERIAL_PORT + 4, 0x0B);
}

/* ── Send a byte over serial ────────────────── */

static void serial_send_byte(uint8_t byte)
{
    /* Wait until transmit buffer is empty */
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);
    outb(SERIAL_PORT, byte);
}

/* ── Emit a telemetry packet ────────────────── */

void telemetry_emit(uint8_t type, const void *data, uint8_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t checksum = 0;
    uint8_t i;
    /* A frame must not be interleaved with one sent from an ISR */
    uint16_t flags = hal_irq_save();

    /* Start byte */
    serial_send_byte(TEL_START_BYTE);

    /* Type */
    serial_send_byte(type);
    checksum ^= type;

    /* Length */
    serial_send_byte(len);
    checksum ^= len;

    /* Data */
    for (i = 0; i < len; i++) {
        serial_send_byte(bytes[i]);
        checksum ^= bytes[i];
    }

    /* Checksum */
    serial_send_byte(checksum);

    hal_irq_restore(flags);
}

/* ── Convenience functions ──────────────────── */

void telemetry_boot_stage(uint8_t stage)
{
    telemetry_emit(TEL_BOOT_STAGE, &stage, 1);
}

void telemetry_thread_event(uint8_t event_type, uint8_t tid)
{
    uint8_t data[2] = { event_type, tid };
    telemetry_emit(TEL_THREAD_EVENT, data, 2);
}

void telemetry_context_switch(uint8_t from_tid, uint8_t to_tid)
{
    uint8_t data[2] = { from_tid, to_tid };
    telemetry_emit(TEL_CONTEXT_SWITCH, data, 2);
}

void telemetry_irq_counters(void)
{
    uint8_t data[8];
    /* Pack the 16-bit counters as little-endian */
    data[0] = (uint8_t)(irq_timer_count & 0xFF);
    data[1] = (uint8_t)((irq_timer_count >> 8) & 0xFF);
    data[2] = (uint8_t)(irq_keyboard_count & 0xFF);
    data[3] = (uint8_t)((irq_keyboard_count >> 8) & 0xFF);
    data[4] = (uint8_t)(irq_syscall_count & 0xFF);
    data[5] = (uint8_t)((irq_syscall_count >> 8) & 0xFF);
    data[6] = (uint8_t)(context_switch_count & 0xFF);
    data[7] = (uint8_t)((context_switch_count >> 8) & 0xFF);
    telemetry_emit(TEL_IRQ_COUNTER, data, 8);
}

void telemetry_fault(uint8_t tid, uint16_t ip, uint16_t cs)
{
    uint8_t data[5];
    data[0] = tid;
    data[1] = (uint8_t)(ip & 0xFF);
    data[2] = (uint8_t)(ip >> 8);
    data[3] = (uint8_t)(cs & 0xFF);
    data[4] = (uint8_t)(cs >> 8);
    telemetry_emit(TEL_FAULT, data, 5);
}
