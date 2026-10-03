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

/* ── Boot information (set by entry.asm) ────── */
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
void telemetry_init(void);
void telemetry_emit(uint8_t type, const void *data, uint8_t len);
void telemetry_boot_stage(uint8_t stage);
void telemetry_thread_event(uint8_t event_type, uint8_t tid);
void telemetry_context_switch(uint8_t from_tid, uint8_t to_tid);
void telemetry_irq_counters(void);
void telemetry_fault(uint8_t tid, uint16_t ip, uint16_t cs);

/* ── Halt the CPU ───────────────────────────── */
extern void halt(void);

#endif /* PHOENIX_KERNEL_H */
