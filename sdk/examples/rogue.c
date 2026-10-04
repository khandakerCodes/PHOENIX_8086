/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK example — rogue
 *
 * A program that breaks the rules on purpose, to show what the kernel
 * notices on a processor that cannot stop it:
 *
 *   1. It hands the kernel bad arguments: pointers outside its own
 *      memory, a made-up semaphore handle, someone else's memory to
 *      free, a thread entry point outside its code. Each call must fail.
 *   2. It recurses until its own stack overflows. The kernel stops it
 *      at the next context switch, before it reaches the stack below.
 *
 * Real mode has no memory protection. None of this stops a program
 * writing over the kernel directly; it only means the kernel itself
 * will not do the damage on a program's behalf.
 */

#include "phoenix.h"

static unsigned refused;
static unsigned tried;

static void print_number(unsigned value)
{
    char digits[6];
    int n = 0;

    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);
    while (n > 0) {
        px_putc(digits[--n]);
    }
}

static void expect_refused(const char *what, unsigned result)
{
    tried++;
    if (result == PX_ERROR) {
        refused++;
    } else {
        px_puts("rogue: NOT refused: ");
        px_puts(what);
        px_putc('\n');
    }
}

/* Recurse forever, yielding at every level so each step is checked */
static unsigned dig(unsigned depth)
{
    volatile char ballast[16];

    ballast[0] = (char)depth;
    px_yield();
    return dig(depth + 1) + ballast[0];
}

int main(void)
{
    unsigned file;

    /* Pointers the program does not own: below its data, past its end */
    expect_refused("puts from offset 0", PX_CALL(0x02, 0, 0x0000, 0, 0));
    expect_refused("puts from offset FFF0", PX_CALL(0x02, 0, 0xFFF0, 0, 0));
    expect_refused("open with a name at FFF0", px_open((const char *)0xFFF0));

    file = px_open("README.TXT");
    expect_refused("read into a buffer at FFF0", px_read(file, (void *)0xFFF0, 64));
    px_close(file);

    /* A handle the kernel never gave out */
    expect_refused("signal a made-up semaphore", PX_CALL(0x0B, 0, 0x1234, 0, 0));
    expect_refused("receive from a made-up mailbox", PX_CALL(0x0E, 0, 0x1234, 0, 0));

    /* Memory that is not this program's to free: the kernel's code, the far arena's start */
    expect_refused("free the kernel's code segment", PX_CALL(0x10, 0, 0x1000, 0, 0));
    expect_refused("free the first far block", PX_CALL(0x10, 0, 0x3001, 0, 0));

    /* A thread that would start outside the program's code */
    expect_refused("start a thread at FFF0", PX_CALL(0x04, 0, 0xFFF0, 5, 0));

    px_puts("rogue: ");
    print_number(refused);
    px_puts(" of ");
    print_number(tried);
    px_puts(" bad calls refused\n");

    px_puts("rogue: now overflowing my own stack\n");
    dig(0);
    px_puts("rogue: still running, which should not happen\n");
    return 0;
}
