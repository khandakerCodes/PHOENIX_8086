/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Serial Port (COM1) Driver
 *
 * Polled 8250/16550 UART driver. Carries the telemetry stream out
 * and dashboard keystrokes in (see telemetry.c).
 */

#include "serial.h"
#include "hal.h"

#define SERIAL_DIVISOR  1       /* 115200 / 115200 */

/* Line status register bits */
#define LSR_DATA_READY  0x01
#define LSR_THR_EMPTY   0x20

void serial_init(void)
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

    /* DTR and RTS set */
    outb(SERIAL_PORT + 4, 0x03);
}

void serial_putc(uint8_t byte)
{
    /* Wait until transmit buffer is empty */
    while ((inb(SERIAL_PORT + 5) & LSR_THR_EMPTY) == 0);
    outb(SERIAL_PORT, byte);
}

bool serial_getc(uint8_t *byte)
{
    if ((inb(SERIAL_PORT + 5) & LSR_DATA_READY) == 0) {
        return false;
    }
    *byte = inb(SERIAL_PORT);
    return true;
}
