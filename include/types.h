/*
 * Phoenix-8086 — Shared Type Definitions
 *
 * Provides standard-width integer types, boolean, and NULL
 * for use across all kernel modules in a freestanding 16-bit
 * real-mode environment (ia16-elf-gcc: int and near pointers
 * are 16 bits, long is 32 bits).
 */

#ifndef PHOENIX_TYPES_H
#define PHOENIX_TYPES_H

#include "layout.h"

/* Exact-width integer types */
typedef unsigned char       uint8_t;
typedef signed char         int8_t;
typedef unsigned short      uint16_t;
typedef signed short        int16_t;
typedef unsigned long       uint32_t;
typedef signed long         int32_t;

/* Size type */
typedef uint16_t            size_t;

/* Boolean */
typedef uint8_t             bool;
#define true                1
#define false               0

/* Null pointer */
#define NULL                ((void *)0)

/* Thread states */
#define THREAD_READY        0
#define THREAD_RUNNING      1
#define THREAD_BLOCKED      2
#define THREAD_SLEEPING     3
#define THREAD_TERMINATED   4

/* Timer */
#define HZ                  100     /* Timer ticks per second (PIT is reprogrammed) */
#define PIT_FREQUENCY       1193182UL

/* System limits */
#define MAX_THREADS         8
#define THREAD_STACK_SIZE   2048    /* 2 KB per thread stack */
#define MAILBOX_SIZE        16

/* Memory map constants (physical addresses) */
#define MEM_IVT_BASE        0x00000
#define MEM_IVT_SIZE        0x00400
#define MEM_BIOS_DATA       0x00400
#define MEM_BIOS_DATA_SIZE  0x00100
#define MEM_BOOTLOADER      0x07C00
#define MEM_STAGE2          0x07E00
#define MEM_KERNEL_CODE     0x10000UL
#define MEM_KERNEL_DATA     0x20000UL
#define MEM_FAR_ARENA       0x30000UL

/* Telemetry message types */
#define TEL_BOOT_STAGE      0x01
#define TEL_THREAD_EVENT    0x02
#define TEL_CONTEXT_SWITCH  0x03
#define TEL_IRQ_COUNTER     0x04
#define TEL_REG_SNAPSHOT    0x05
#define TEL_MEM_SUMMARY     0x06
#define TEL_FAULT           0x07

/* Telemetry framing */
#define TEL_START_BYTE      0xFE
#define TEL_SERIAL_PORT     0x3F8  /* COM1 */

#endif /* PHOENIX_TYPES_H */
