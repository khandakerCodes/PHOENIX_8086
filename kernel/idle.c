/*
 * Phoenix-8086 — Idle Thread
 *
 * The idle thread runs when no other thread is READY.
 * It executes HLT in a loop to save power and reduce
 * CPU heat while waiting for the next interrupt.
 */

#include "../include/types.h"

void idle_thread(void)
{
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
