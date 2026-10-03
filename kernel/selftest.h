/*
 * Phoenix-8086 — Kernel Self-Test Header
 */

#ifndef PHOENIX_SELFTEST_H
#define PHOENIX_SELFTEST_H

#include "../include/types.h"

/* Run all self-tests from a thread; prints a summary and returns the failure count */
uint16_t selftest_run(void);

#endif /* PHOENIX_SELFTEST_H */
