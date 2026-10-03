/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Disk Access Header
 */

#ifndef PHOENIX_DISK_H
#define PHOENIX_DISK_H

#include "../include/types.h"

#define DISK_SECTOR_SIZE    512

/* Set the drive geometry used to turn sector numbers into cylinder/head/sector */
void disk_set_geometry(uint16_t sectors_per_track, uint16_t heads);

/*
 * Read one sector of the boot drive into a 512-byte buffer in the
 * kernel data segment. Returns false after three failed attempts.
 */
bool disk_read_sector(uint16_t lba, uint8_t *buffer);

#endif /* PHOENIX_DISK_H */
