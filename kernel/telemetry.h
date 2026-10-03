/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Telemetry Header
 *
 * Telemetry protocol v1 (see docs/telemetry.md). The kernel records
 * events into a ring buffer; a kernel thread frames them and sends
 * them over COM1. Nothing here does serial I/O from an interrupt
 * handler.
 *
 * Build with TELEMETRY=0 to compile telemetry out: every call below
 * becomes an empty inline function and the console is mirrored to
 * the serial port as plain text instead.
 */

#ifndef PHOENIX_TELEMETRY_H
#define PHOENIX_TELEMETRY_H

#include "../include/types.h"
#include "tcb.h"

#define TEL_PROTOCOL_VERSION    1

/* Framing */
#define TEL_FRAME_DELIMITER     0x7E
#define TEL_FRAME_ESCAPE        0x7D
#define TEL_FRAME_ESCAPE_XOR    0x20

/* Record types */
#define TEL_HELLO               0x00
#define TEL_BOOT_STAGE          0x01
#define TEL_THREAD_CREATE       0x02
#define TEL_THREAD_EXIT         0x03
#define TEL_THREAD_STATE        0x04
#define TEL_CONTEXT_SWITCH      0x05
#define TEL_COUNTERS            0x06
#define TEL_MEMORY              0x07
#define TEL_FAULT               0x08
#define TEL_CONSOLE             0x09
#define TEL_SYSCALL             0x0A
#define TEL_BENCH               0x0B
#define TEL_THREAD_STATS        0x0C

/* Largest record payload */
#define TEL_MAX_PAYLOAD         64

/* Benchmark kinds */
#define TEL_BENCH_SWITCHES      0
#define TEL_BENCH_HEAP          1

#if CONFIG_TELEMETRY

/* Reset the ring buffer (the serial port must already be set up) */
void telemetry_init(void);

/* Start the telemetry thread (needs the scheduler) */
void telemetry_start(void);

/* Events. All are safe from interrupt handlers and never block. */
void telemetry_boot_stage(uint8_t stage);
void telemetry_thread_created(uint8_t tid);
void telemetry_thread_exited(uint8_t tid);
void telemetry_thread_state(uint8_t tid);
void telemetry_context_switch(uint8_t from_tid, uint8_t to_tid, uint16_t to_sp);
void telemetry_syscall(uint8_t tid, uint8_t func);
void telemetry_fault(uint8_t tid, const frame_t *frame, const char *reason);
void telemetry_bench(uint8_t kind, uint32_t count);
void telemetry_console_char(char c);

/* Send everything buffered right now, with interrupts off (used by panic) */
void telemetry_flush(void);

#else

static inline void telemetry_init(void) {}
static inline void telemetry_start(void) {}
static inline void telemetry_boot_stage(uint8_t stage) { (void)stage; }
static inline void telemetry_thread_created(uint8_t tid) { (void)tid; }
static inline void telemetry_thread_exited(uint8_t tid) { (void)tid; }
static inline void telemetry_thread_state(uint8_t tid) { (void)tid; }
static inline void telemetry_context_switch(uint8_t from_tid, uint8_t to_tid, uint16_t to_sp)
{ (void)from_tid; (void)to_tid; (void)to_sp; }
static inline void telemetry_syscall(uint8_t tid, uint8_t func) { (void)tid; (void)func; }
static inline void telemetry_fault(uint8_t tid, const frame_t *frame, const char *reason)
{ (void)tid; (void)frame; (void)reason; }
static inline void telemetry_bench(uint8_t kind, uint32_t count) { (void)kind; (void)count; }
static inline void telemetry_flush(void) {}

#endif /* CONFIG_TELEMETRY */

#endif /* PHOENIX_TELEMETRY_H */
