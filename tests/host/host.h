/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Host test shim
 *
 * Force-included (-include) before every kernel file built for the
 * host tests, together with -DPHOENIX_HOST (include/types.h) and
 * -DCONFIG_TELEMETRY=0. It makes the 16-bit kernel's far pointers
 * work on a 64-bit host: `__far` disappears, and segment:offset
 * addresses land in a simulated megabyte of memory.
 */

#ifndef PHOENIX_HOST_SHIM_H
#define PHOENIX_HOST_SHIM_H

#include <stdint.h>

#define __far

/* One megabyte of real-mode memory plus the 64 KB that segment FFFF reaches past it */
#define HOST_MEMORY_SIZE    0x110000
extern unsigned char host_memory[HOST_MEMORY_SIZE];

#define MK_FP(seg, off) \
    ((void *)(host_memory + (((uint32_t)(uint16_t)(seg) << 4) + (uint16_t)(off))))

#endif /* PHOENIX_HOST_SHIM_H */
