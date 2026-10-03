/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Floppy Disk Controller Driver Header
 */

#ifndef PHOENIX_FLOPPY_H
#define PHOENIX_FLOPPY_H

#include "../include/types.h"

/*
 * Find and reset the floppy controller. Returns false if there is none
 * (or it does not answer), in which case the disk layer keeps using
 * the BIOS.
 */
bool floppy_init(void);

/* Reset the controller again, e.g. after the BIOS has used the drive */
bool floppy_reset(void);

/*
 * Read one sector of drive A: into a 512-byte buffer in the kernel
 * data segment. The calling thread sleeps while the drive works.
 */
bool floppy_read_sector(uint8_t cylinder, uint8_t head, uint8_t sector, uint8_t *buffer);

/* Called from the timer interrupt: stops the motor after a pause in use */
void floppy_tick(void);

/* IRQ6 handler (see isr.S) */
uint16_t floppy_handler(uint16_t sp);

#endif /* PHOENIX_FLOPPY_H */
