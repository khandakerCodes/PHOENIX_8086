/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Memory Routines the Compiler Expects
 *
 * Even in a freestanding build, GCC may call memcpy and memset on its
 * own, for example to copy a structure. The kernel has no C library,
 * so it provides them.
 */

#include "../include/types.h"

void *memcpy(void *dest, const void *src, size_t count)
{
    uint8_t *d = dest;
    const uint8_t *s = src;

    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *dest, int value, size_t count)
{
    uint8_t *d = dest;

    while (count--) {
        *d++ = (uint8_t)value;
    }
    return dest;
}
