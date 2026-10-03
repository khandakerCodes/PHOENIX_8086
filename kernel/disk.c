/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Disk Access
 *
 * Reads sectors of the boot drive through one of two drivers:
 *
 * Native (floppy.c): the kernel programs the floppy controller itself.
 *   The calling thread sleeps while the drive works and everything
 *   else keeps running. Used when the boot drive is A: and a
 *   controller answers.
 *
 * BIOS (INT 13h): the fallback, for machines and emulators without a
 *   controller the native driver can use. The BIOS only works while it
 *   owns the timer and keyboard interrupts, so for each read the
 *   kernel hands those back, makes the call, and takes them again
 *   (irq_bios_enter/irq_bios_leave in interrupts.c). During a BIOS
 *   read nothing is scheduled, the tick counter stops, and keys
 *   pressed are lost.
 *
 * Reads always go into the kernel data segment. That segment lies
 * inside one 64 KB physical page, which the PC's floppy DMA requires.
 */

#include "disk.h"
#include "floppy.h"
#include "interrupts.h"
#include "kernel.h"
#include "hal.h"

#define DISK_RETRIES    3

static uint8_t driver = DISK_BIOS;

/* 1.44 MB floppy until the file system says otherwise */
static uint16_t geometry_sectors = 18;
static uint16_t geometry_heads = 2;

void disk_init(void)
{
    /* The native driver handles drive A: only */
    driver = (boot_drive == 0 && floppy_init()) ? DISK_NATIVE : DISK_BIOS;
}

uint8_t disk_driver(void)
{
    return driver;
}

const char *disk_driver_name(void)
{
    return driver == DISK_NATIVE ? "native floppy driver" : "BIOS INT 13h";
}

bool disk_select(uint8_t wanted)
{
    if (wanted == DISK_NATIVE) {
        /* The BIOS may have moved the head and reprogrammed the controller */
        if (boot_drive != 0 || !floppy_init()) {
            return false;
        }
    } else if (driver == DISK_NATIVE) {
        irq_release(6, FLOPPY_VECTOR);
    }
    driver = wanted;
    return true;
}

void disk_set_geometry(uint16_t sectors_per_track, uint16_t heads)
{
    if (sectors_per_track && heads) {
        geometry_sectors = sectors_per_track;
        geometry_heads = heads;
    }
}

static bool bios_read(uint16_t cylinder, uint16_t head, uint16_t sector, uint8_t *buffer)
{
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

bool disk_read_sector(uint16_t lba, uint8_t *buffer)
{
    uint16_t track = lba / geometry_sectors;
    uint16_t sector = lba % geometry_sectors + 1;
    uint16_t cylinder = track / geometry_heads;
    uint16_t head = track % geometry_heads;

    if (driver == DISK_NATIVE) {
        return floppy_read_sector((uint8_t)cylinder, (uint8_t)head, (uint8_t)sector, buffer);
    }
    return bios_read(cylinder, head, sector, buffer);
}
