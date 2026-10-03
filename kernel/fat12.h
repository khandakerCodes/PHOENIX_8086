/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — FAT12 File System Header (read-only)
 */

#ifndef PHOENIX_FAT12_H
#define PHOENIX_FAT12_H

#include "../include/types.h"

#define FAT_NAME_MAX    13      /* "FILENAME.EXT" + NUL */

typedef struct {
    char     name[FAT_NAME_MAX];
    uint32_t size;
    uint16_t first_cluster;
} fat_dirent_t;

typedef struct {
    uint32_t size;
    uint32_t position;
    uint16_t cluster;           /* Cluster that holds `position` */
} fat_file_t;

/* Read the boot sector and the FAT. Returns false if the disk is not FAT12. */
bool fat_mount(void);

/* True once fat_mount has succeeded */
bool fat_mounted(void);

/*
 * Walk the root directory. Start with *index = 0; each call fills in
 * the next file and advances *index. Returns false at the end.
 */
bool fat_next(uint16_t *index, fat_dirent_t *entry);

/* Open a file in the root directory by name (case-insensitive) */
bool fat_open(const char *name, fat_file_t *file);

/* Read up to `length` bytes at the file's position; returns the number read */
uint16_t fat_read(fat_file_t *file, uint8_t *buffer, uint16_t length);

#endif /* PHOENIX_FAT12_H */
