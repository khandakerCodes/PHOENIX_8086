/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Serial Port (COM1) Header
 */

#ifndef PHOENIX_SERIAL_H
#define PHOENIX_SERIAL_H

#include "../include/types.h"

#define SERIAL_PORT     0x3F8   /* COM1 */

/* Set up COM1: 115200 baud, 8N1, polled (no UART interrupts) */
void serial_init(void);

/* Send one byte, waiting for the transmitter if necessary */
void serial_putc(uint8_t byte);

/* Fetch a received byte if one is waiting; returns false otherwise */
bool serial_getc(uint8_t *byte);

#endif /* PHOENIX_SERIAL_H */
