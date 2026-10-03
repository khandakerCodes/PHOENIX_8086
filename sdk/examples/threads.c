/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK example — threads
 *
 * A program with several threads. Two workers send numbers through a
 * mailbox; the main thread adds them up. A semaphore tells the main
 * thread when both workers are done.
 *
 * The main thread returns while far memory it allocated is still
 * outstanding: the kernel reclaims it, along with the program's code
 * and data once the last thread has ended.
 */

#include "phoenix.h"

#define NUMBERS_PER_WORKER  5

static unsigned mailbox;
static unsigned finished;

static void print_number(unsigned value)
{
    char digits[6];
    int count = 0;

    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (count) {
        px_putc(digits[--count]);
    }
}

/* Sends 1..5, pausing so the two workers interleave */
static void low_worker(void)
{
    unsigned i;

    for (i = 1; i <= NUMBERS_PER_WORKER; i++) {
        px_mbox_send(mailbox, i);
        px_sleep(3);
    }
    px_sem_signal(finished);
}

/* Sends 100..500 */
static void high_worker(void)
{
    unsigned i;

    for (i = 1; i <= NUMBERS_PER_WORKER; i++) {
        px_mbox_send(mailbox, i * 100);
        px_sleep(2);
    }
    px_sem_signal(finished);
}

int main(void)
{
    unsigned total = 0;
    unsigned i;

    mailbox = px_mbox_create();
    finished = px_sem_create(0);
    if (mailbox == PX_ERROR || finished == PX_ERROR) {
        px_puts("threads: out of memory\n");
        return 1;
    }

    if (px_thread_create(low_worker, 6) == PX_ERROR ||
        px_thread_create(high_worker, 6) == PX_ERROR) {
        px_puts("threads: cannot create a thread\n");
        return 1;
    }

    for (i = 0; i < 2 * NUMBERS_PER_WORKER; i++) {
        total += px_mbox_recv(mailbox);
    }
    px_sem_wait(finished);
    px_sem_wait(finished);

    px_puts("threads: two workers sent ");
    print_number(2 * NUMBERS_PER_WORKER);
    px_puts(" numbers, total ");
    print_number(total);
    px_putc('\n');

    px_sem_destroy(finished);
    px_mbox_destroy(mailbox);

    px_alloc(64);       /* never freed: reclaimed when this thread ends */
    return 0;
}
