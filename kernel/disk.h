/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Disk Access Header
 */

#ifndef PHOENIX_DISK_H
#define PHOENIX_DISK_H

#include "../include/types.h"

#define DISK_SECTOR_SIZE    512

/* How sectors are read */
#define DISK_NATIVE     0   /* The kernel's own floppy controller driver */
#define DISK_BIOS       1   /* BIOS INT 13h */

/* Choose a driver: the native one if a controller answers, otherwise the BIOS */
void disk_init(void);

/* The driver in use (DISK_NATIVE or DISK_BIOS) and its name */
uint8_t disk_driver(void);
const char *disk_driver_name(void);

/* Switch driver; returns false if the native driver finds no controller */
bool disk_select(uint8_t driver);

/* Set the drive geometry used to turn sector numbers into cylinder/head/sector */
void disk_set_geometry(uint16_t sectors_per_track, uint16_t heads);

/*
 * Read one sector of the boot drive into a 512-byte buffer in the
 * kernel data segment. Returns false after three failed attempts.
 */
bool disk_read_sector(uint16_t lba, uint8_t *buffer);

#endif /* PHOENIX_DISK_H */
