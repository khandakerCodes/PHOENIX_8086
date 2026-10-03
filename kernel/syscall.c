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
 * A pointer argument is an offset in the caller's own data segment,
 * which for a loaded program is not the kernel's. The kernel reaches
 * it through a far pointer built from the caller's saved DS.
 *
 * Semaphore and mailbox handles are near pointers to kernel heap
 * objects, opaque to the caller. Real mode has no memory protection,
 * so they are not validated beyond a NULL check.
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
#include "fat12.h"
#include "exec.h"
#include "hal.h"

extern void thread_terminate(int tid);

/* ── The caller's memory ────────────────────── */

#define NAME_MAX_LENGTH     13      /* "FILENAME.EXT" + NUL */
#define PUTS_MAX_LENGTH     1024    /* A string without a terminator must not print forever */
#define READ_CHUNK          64

/* Copy a short string argument out of the caller's data segment */
static void copy_name(const frame_t *frame, uint16_t offset, char *out)
{
    const char __far *in = (const char __far *)MK_FP(frame->ds, offset);
    uint8_t i;

    for (i = 0; i < NAME_MAX_LENGTH - 1 && in[i]; i++) {
        out[i] = in[i];
    }
    out[i] = '\0';
}

/* ── Open files ─────────────────────────────── */

/* A small system-wide table; a handle is an index into it */
static struct {
    bool       used;
    int        owner;
    fat_file_t file;
} open_files[MAX_OPEN_FILES];

static int file_open(const char *name)
{
    uint16_t flags;
    fat_file_t file;
    int handle;

    if (!fat_open(name, &file)) {
        return -1;
    }

    flags = hal_irq_save();
    for (handle = 0; handle < MAX_OPEN_FILES; handle++) {
        if (!open_files[handle].used) {
            open_files[handle].used  = true;
            open_files[handle].owner = thread_current_tid();
            open_files[handle].file  = file;
            break;
        }
    }
    hal_irq_restore(flags);

    return handle < MAX_OPEN_FILES ? handle : -1;
}

static bool file_valid(uint16_t handle)
{
    return handle < MAX_OPEN_FILES && open_files[handle].used &&
           open_files[handle].owner == thread_current_tid();
}

void file_close_owned(int tid)
{
    int handle;

    for (handle = 0; handle < MAX_OPEN_FILES; handle++) {
        if (open_files[handle].used && open_files[handle].owner == tid) {
            open_files[handle].used = false;
        }
    }
}

bool syscall_dispatch(frame_t *frame)
{
    uint8_t func = (uint8_t)(frame->ax >> 8);  /* Function number in AH */
    bool resched = false;
    bool error = false;
    char name[NAME_MAX_LENGTH];
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
        /* BX = offset of a null-terminated string in the caller's data segment */
        {
            const char __far *text = (const char __far *)MK_FP(frame->ds, frame->bx);
            uint16_t i;

            for (i = 0; i < PUTS_MAX_LENGTH && text[i]; i++) {
                con_putchar(text[i]);
            }
        }
        break;

    case SYS_THREAD_CREATE: {
        /*
         * BX = entry address in the caller's code segment, CL = priority,
         * DX = a value handed to the new thread in SI.
         */
        tcb_t *caller = thread_get_tcb(thread_current_tid());

        if (caller->program && frame->cs == caller->program->code_segment) {
            /* A program starting another thread in itself */
            tid = thread_create_program(caller->program, frame->bx, frame->dx,
                                        (uint8_t)(frame->cx & 0xFF), caller->name);
        } else {
            tid = thread_create((void (*)(void))frame->bx,
                                (uint8_t)(frame->cx & 0xFF), NULL);
        }
        if (tid < 0) {
            error = true;
        }
        frame->ax = (uint16_t)tid;
        break;
    }

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
        /* Owned by the caller, so it is reclaimed if the thread ends without freeing it */
        frame->ax = far_alloc_owned(frame->bx, (uint8_t)thread_current_tid());
        if (frame->ax == 0) {
            error = true;
        }
        break;

    case SYS_FREE:
        /* BX = segment */
        far_free(frame->bx);
        break;

    case SYS_SEM_DESTROY:
        /* Refused while threads wait on it: they would never wake */
        if (frame->bx && ((semaphore_t *)frame->bx)->wait_count == 0) {
            kfree((void *)frame->bx);
        } else {
            error = true;
        }
        break;

    case SYS_MBOX_DESTROY:
        if (frame->bx && ((mailbox_t *)frame->bx)->items.wait_count == 0 &&
            ((mailbox_t *)frame->bx)->space.wait_count == 0) {
            kfree((void *)frame->bx);
        } else {
            error = true;
        }
        break;

    case SYS_OPEN:
        /* BX = offset of the file name */
        copy_name(frame, frame->bx, name);
        tid = file_open(name);
        if (tid < 0) {
            error = true;
        }
        frame->ax = (uint16_t)tid;
        break;

    case SYS_READ:
        /* BX = handle, CX = length, DX = offset of the buffer */
        if (file_valid(frame->bx)) {
            /* The file system reads into kernel memory; pass it on in pieces */
            uint8_t __far *out = (uint8_t __far *)MK_FP(frame->ds, frame->dx);
            uint8_t chunk[READ_CHUNK];
            uint16_t total = 0;

            while (total < frame->cx) {
                uint16_t want = frame->cx - total;
                uint16_t got, i;

                if (want > READ_CHUNK) want = READ_CHUNK;
                got = fat_read(&open_files[frame->bx].file, chunk, want);
                for (i = 0; i < got; i++) {
                    out[total + i] = chunk[i];
                }
                total += got;
                if (got < want) break;
            }
            frame->ax = total;
        } else {
            error = true;
        }
        break;

    case SYS_CLOSE:
        if (file_valid(frame->bx)) {
            open_files[frame->bx].used = false;
        } else {
            error = true;
        }
        break;

    case SYS_EXEC:
        /* BX = offset of the program file name */
        copy_name(frame, frame->bx, name);
        tid = exec_program(name, NULL);
        if (tid < 0) {
            error = true;
        }
        frame->ax = (uint16_t)tid;
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
