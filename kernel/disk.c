/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Disk Access
 *
 * The kernel has no floppy controller driver. It reads the boot drive
 * through the BIOS (INT 13h), which only works while the BIOS owns the
 * timer and keyboard interrupts. So for each read the kernel hands
 * those back, makes the call, and takes them again
 * (irq_bios_enter/irq_bios_leave in interrupts.c).
 *
 * Consequences, by design for now:
 *   - nothing is scheduled during a read, and the tick counter stops;
 *   - keys pressed during a read go to the BIOS and are lost;
 *   - the BIOS runs on the calling thread's stack.
 *
 * Reads always go into the kernel data segment. That segment lies
 * inside one 64 KB physical page, which the PC's floppy DMA requires.
 */

#include "disk.h"
#include "interrupts.h"
#include "kernel.h"
#include "hal.h"

#define DISK_RETRIES    3

/* 1.44 MB floppy until the file system says otherwise */
static uint16_t geometry_sectors = 18;
static uint16_t geometry_heads = 2;

void disk_set_geometry(uint16_t sectors_per_track, uint16_t heads)
{
    if (sectors_per_track && heads) {
        geometry_sectors = sectors_per_track;
        geometry_heads = heads;
    }
}

bool disk_read_sector(uint16_t lba, uint8_t *buffer)
{
    uint16_t track = lba / geometry_sectors;
    uint16_t sector = lba % geometry_sectors + 1;
    uint16_t cylinder = track / geometry_heads;
    uint16_t head = track % geometry_heads;
    uint16_t flags = hal_irq_save();
    uint16_t status = 1;
    uint8_t attempt;

    irq_bios_enter();

    for (attempt = 0; attempt < DISK_RETRIES && status != 0; attempt++) {
        status = bios_disk_read(boot_drive, cylinder, head, sector, buffer);
        if (status != 0) {
            bios_disk_reset(boot_drive);
        }
    }

    irq_bios_leave();
    hal_irq_restore(flags);

    return status == 0;
}
