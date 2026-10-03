/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK example — greet
 *
 * Reads a key and counts to three. While it waits for the key it has
 * the keyboard: a key goes to whichever thread asked for one last, so
 * the shell does not see it. While it sleeps, other threads run.
 */

#include "phoenix.h"

int main(void)
{
    px_puts("What is your favourite number? ");
    char digit = px_getc();          /* waits for a key */
    px_putc(digit);
    px_puts("\nGood choice. Counting to three:\n");

    for (char n = '1'; n <= '3'; n++) {
        px_sleep(PX_HZ / 2);         /* half a second; other threads run meanwhile */
        px_putc(n);
        px_putc('\n');
    }
    return 0;
}
