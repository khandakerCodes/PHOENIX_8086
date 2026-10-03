/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK example — hello
 *
 * The smallest useful program: print a message through the kernel.
 */

#include "phoenix.h"

int main(void)
{
    px_puts("Hello from a program loaded off the disk!\n");
    px_puts("System call ABI version ");
    px_putc('0' + px_version());
    px_putc('\n');
    return 0;
}
