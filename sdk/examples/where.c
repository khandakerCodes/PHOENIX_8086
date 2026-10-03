/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK example — where
 *
 * Shows where the kernel loaded this program: its code segment and
 * its data segment, which is also its stack segment. It then uses
 * 50 KB of arrays, more than the kernel's whole heap, to show that a
 * program's memory is its own. (One object cannot exceed 32,767 bytes
 * with this compiler, so there are two.)
 */

#include "phoenix.h"

#define FIRST   30000u
#define SECOND  20000u

/* bss, in this program's data segment */
static unsigned char first[FIRST];
static unsigned char second[SECOND];

static void print_hex(unsigned value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    for (shift = 12; shift >= 0; shift -= 4) {
        px_putc(digits[(value >> shift) & 0xF]);
    }
}

int main(void)
{
    unsigned cs, ds, ss;
    unsigned i, sum = 0;

    __asm__("movw %%cs, %0" : "=r"(cs));
    __asm__("movw %%ds, %0" : "=r"(ds));
    __asm__("movw %%ss, %0" : "=r"(ss));

    px_puts("where: cs=");
    print_hex(cs);
    px_puts(" ds=");
    print_hex(ds);
    px_puts(" ss=");
    print_hex(ss);
    px_putc('\n');

    /* The loader zeroed the arrays; read every byte, then write the two far ends */
    for (i = 0; i < FIRST; i++) {
        sum += first[i];
    }
    for (i = 0; i < SECOND; i++) {
        sum += second[i];
    }
    first[0] = 1;
    second[SECOND - 1] = 2;
    sum += first[0] + second[SECOND - 1];

    px_puts(sum == 3 ? "where: 50000 bytes of my own, zeroed and writable\n"
                     : "where: memory check FAILED\n");
    return 0;
}
