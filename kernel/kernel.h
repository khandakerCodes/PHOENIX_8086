/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Kernel Header
 *
 * Top-level kernel declarations, boot info, and
 * forward declarations for all subsystem init functions.
 */

#ifndef PHOENIX_KERNEL_H
#define PHOENIX_KERNEL_H

#include "../include/types.h"

/* ── Version ────────────────────────────────── */
#define PHOENIX_VERSION "0.6-dev"

/* ── Boot information (set by entry.S) ──────── */
extern uint8_t  boot_drive;
extern uint16_t mem_kb;

/* ── Subsystem initialization ───────────────── */
void con_init(void);
void irq_init(void);
void kb_init(void);
void mem_init(void);
void sched_init(void);
void shell_run(void);

/* ── Telemetry subsystem ─────────────────────── */
#include "telemetry.h"

/* ── Halt the CPU ───────────────────────────── */
extern void halt(void);

#endif /* PHOENIX_KERNEL_H */
