/*
 * Phoenix-8086 — Kernel Panic Header
 */

#ifndef PHOENIX_PANIC_H
#define PHOENIX_PANIC_H

#include "../include/types.h"

/* Software interrupt used by kernel_panic() to capture registers */
#define PANIC_VECTOR    0x82

/* Install the CPU exception traps and the panic vector (call early in boot) */
void panic_init(void);

/* Trigger a kernel panic with a reason string; does not return */
void kernel_panic(const char *reason);

#endif /* PHOENIX_PANIC_H */
