/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — System Call Header
 *
 * INT 80h, function number in AH. Arguments in AL/BX/CX/DX,
 * result in AX (DX:AX for 32-bit results). The carry flag is
 * set on error and clear on success. See docs/syscalls.md.
 */

#ifndef PHOENIX_SYSCALL_H
#define PHOENIX_SYSCALL_H

#include "../include/types.h"
#include "tcb.h"

#define SYSCALL_ABI_VERSION 1

/* System call numbers (placed in AH before INT 80h) */
#define SYS_VERSION         0x00    /* → AX = ABI version */
#define SYS_PUTC            0x01    /* AL = character */
#define SYS_PUTS            0x02    /* BX = near pointer to string */
#define SYS_GETC            0x03    /* → AL = character (blocks) */
#define SYS_THREAD_CREATE   0x04    /* BX = entry, CL = priority → AX = TID */
#define SYS_THREAD_EXIT     0x05
#define SYS_YIELD           0x06
#define SYS_SLEEP           0x07    /* CX = ticks */
#define SYS_TICKS           0x08    /* → DX:AX = tick count */
#define SYS_SEM_CREATE      0x09    /* BX = initial count → AX = handle */
#define SYS_SEM_WAIT        0x0A    /* BX = handle (blocks) */
#define SYS_SEM_SIGNAL      0x0B    /* BX = handle */
#define SYS_MBOX_CREATE     0x0C    /* → AX = handle */
#define SYS_MBOX_SEND       0x0D    /* BX = handle, CX = message (blocks) */
#define SYS_MBOX_RECV       0x0E    /* BX = handle → AX = message (blocks) */
#define SYS_ALLOC           0x0F    /* BX = paragraphs → AX = segment */
#define SYS_FREE            0x10    /* BX = segment */

/* AX value returned with the carry flag set */
#define SYS_ERROR           0xFFFF

/* Build the AX value for a call */
#define SYS_AX(func, al)    (((uint16_t)(func) << 8) | (uint8_t)(al))

/*
 * Dispatch a system call (called from the INT 80h handler with
 * interrupts off). Reads arguments from and writes results to the
 * caller's saved register frame. Returns true if the scheduler
 * must run before returning to the caller.
 */
bool syscall_dispatch(frame_t *frame);

#endif /* PHOENIX_SYSCALL_H */
