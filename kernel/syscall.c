/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — System Call Implementation
 *
 * Dispatches INT 80h system calls based on the function
 * number in AH. Provides access to kernel services: printing,
 * thread management, sleeping, tick queries, synchronisation,
 * message passing, and far memory.
 *
 * The handler runs on the calling thread's own stack, so a call
 * that has to wait (getc, sem_wait, mailbox send/receive) simply
 * calls the normal blocking kernel function: the thread is switched
 * out in the middle of its system call and finishes it when woken.
 *
 * Semaphore and mailbox handles are near pointers to kernel heap
 * objects. Real mode has no memory protection, so they are not
 * validated beyond a NULL check.
 */

#include "syscall.h"
#include "console.h"
#include "thread.h"
#include "scheduler.h"
#include "interrupts.h"
#include "keyboard.h"
#include "sync.h"
#include "ipc.h"
#include "memory.h"

extern void thread_terminate(int tid);

bool syscall_dispatch(frame_t *frame)
{
    uint8_t func = (uint8_t)(frame->ax >> 8);  /* Function number in AH */
    bool resched = false;
    bool error = false;
    int tid;
    void *obj;

    /* Success unless a case says otherwise */
    frame->flags &= ~FLAGS_CF;

    switch (func) {
    case SYS_VERSION:
        frame->ax = SYSCALL_ABI_VERSION;
        break;

    case SYS_PUTC:
        /* AL = character to print */
        con_putchar((char)(frame->ax & 0xFF));
        break;

    case SYS_PUTS:
        /* BX = near pointer to a null-terminated string */
        con_print((const char *)frame->bx);
        break;

    case SYS_THREAD_CREATE:
        /* BX = entry address, CL = priority */
        tid = thread_create((void (*)(void))frame->bx,
                            (uint8_t)(frame->cx & 0xFF), NULL);
        if (tid < 0) {
            error = true;
        }
        frame->ax = (uint16_t)tid;
        break;

    case SYS_THREAD_EXIT:
        /* The scheduler switches away and never resumes this frame */
        thread_terminate(thread_current_tid());
        resched = true;
        break;

    case SYS_YIELD:
        sched_yield_current();
        resched = true;
        break;

    case SYS_SLEEP:
        /* CX = number of ticks to sleep */
        sched_sleep(thread_current_tid(), frame->cx);
        resched = true;
        break;

    case SYS_TICKS:
        /* Interrupts are off here, so the 32-bit read is consistent */
        frame->ax = (uint16_t)(tick_count & 0xFFFF);
        frame->dx = (uint16_t)(tick_count >> 16);
        break;

    case SYS_GETC:
        /* Blocks until a key is available */
        frame->ax = (uint8_t)kb_getchar();
        break;

    case SYS_SEM_CREATE:
        /* BX = initial count */
        obj = kmalloc(sizeof(semaphore_t));
        if (obj) {
            sem_init((semaphore_t *)obj, (int16_t)frame->bx);
            frame->ax = (uint16_t)obj;
        } else {
            error = true;
        }
        break;

    case SYS_SEM_WAIT:
        if (frame->bx) {
            sem_wait((semaphore_t *)frame->bx);
        } else {
            error = true;
        }
        break;

    case SYS_SEM_SIGNAL:
        if (frame->bx) {
            sem_signal((semaphore_t *)frame->bx);
        } else {
            error = true;
        }
        break;

    case SYS_MBOX_CREATE:
        obj = kmalloc(sizeof(mailbox_t));
        if (obj) {
            mbox_init((mailbox_t *)obj);
            frame->ax = (uint16_t)obj;
        } else {
            error = true;
        }
        break;

    case SYS_MBOX_SEND:
        /* BX = handle, CX = message */
        if (frame->bx) {
            mbox_send((mailbox_t *)frame->bx, frame->cx);
        } else {
            error = true;
        }
        break;

    case SYS_MBOX_RECV:
        /* BX = handle */
        if (frame->bx) {
            frame->ax = mbox_recv((mailbox_t *)frame->bx);
        } else {
            error = true;
        }
        break;

    case SYS_ALLOC:
        /* BX = paragraphs */
        frame->ax = far_alloc(frame->bx);
        if (frame->ax == 0) {
            error = true;
        }
        break;

    case SYS_FREE:
        /* BX = segment */
        far_free(frame->bx);
        break;

    default:
        /* Unknown call */
        error = true;
        break;
    }

    if (error) {
        frame->flags |= FLAGS_CF;
        frame->ax = SYS_ERROR;
    }

    return resched;
}
