/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK example — clock
 *
 * Sleeps between lines, so the scheduler runs other threads meanwhile,
 * then reads a file from the boot disk through the file system calls.
 */

#include "phoenix.h"

static char buffer[32];

int main(void)
{
    unsigned long start = px_ticks();
    unsigned file, got, i;

    for (i = 1; i <= 3; i++) {
        px_sleep(PX_HZ / 4);
        px_puts("tick ");
        px_putc('0' + i);
        px_putc('\n');
    }

    px_puts("elapsed ticks: ");
    i = (unsigned)(px_ticks() - start);
    px_putc('0' + i / 10 % 10);
    px_putc('0' + i % 10);
    px_putc('\n');

    file = px_open("README.TXT");
    if (file == PX_ERROR) {
        px_puts("cannot open README.TXT\n");
        return 1;
    }
    got = px_read(file, buffer, 22);
    px_close(file);

    px_puts("first line of README.TXT: ");
    for (i = 0; i < got && buffer[i] != '\n' && buffer[i] != '\r'; i++) {
        px_putc(buffer[i]);
    }
    px_putc('\n');
    return 0;
}
