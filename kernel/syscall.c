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
 * Arguments from a loaded program are checked before they are used
 * (docs/syscalls.md, "Argument checks"): pointers must lie inside the
 * program's own data, thread entry points inside its own code, handles
 * must be ones the kernel gave out, and memory it frees must be its
 * own. A refused call fails like any other and is reported in
 * telemetry. Kernel threads are trusted. None of this is protection:
 * real mode lets a program write anywhere without asking the kernel.
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
#include "telemetry.h"

extern void thread_terminate(int tid);

/* ── The caller's memory ────────────────────── */

#define NAME_MAX_LENGTH     13      /* "FILENAME.EXT" + NUL */
#define PUTS_MAX_LENGTH     1024    /* A string without a terminator must not print forever */
#define READ_CHUNK          64

/* The program the calling thread runs in, or NULL for a kernel thread */
static program_t *caller_program(void)
{
    return thread_get_tcb(thread_current_tid())->program;
}

/*
 * Bytes of the caller's own memory from `offset` on: 0 if `offset` is
 * not in it. A program owns its data segment from PROG_DATA_START to
 * the end of its stacks, and must pass pointers relative to it.
 */
static uint16_t caller_room(const frame_t *frame, uint16_t offset)
{
    const program_t *program = caller_program();

    if (!program) {
        return 0xFFFF - offset;     /* A kernel thread: the whole segment */
    }
    if (frame->ds != program->data_segment ||
        offset < PROG_DATA_START || offset >= program->data_limit) {
        return 0;
    }
    return program->data_limit - offset;
}

/* Is the buffer of `length` bytes at `offset` all in the caller's memory? */
static bool caller_owns(const frame_t *frame, uint16_t offset, uint16_t length)
{
    uint16_t room = caller_room(frame, offset);

    return room > 0 && length <= room;
}

/* Copy a short string argument out of the caller's data segment */
static bool copy_name(const frame_t *frame, uint16_t offset, char *out)
{
    const char __far *in = (const char __far *)MK_FP(frame->ds, offset);
    uint16_t room = caller_room(frame, offset);
    uint8_t i;

    for (i = 0; i < NAME_MAX_LENGTH - 1 && i < room && in[i]; i++) {
        out[i] = in[i];
    }
    out[i] = '\0';
    return room > 0;
}

/* ── Semaphore and mailbox handles ──────────── */

/*
 * A handle is the object's address in the kernel's data segment. Every
 * object made through a system call is listed here, so a handle the
 * kernel never gave out is refused rather than used as a pointer into
 * kernel memory. What a program creates is freed when it ends.
 */
#define MAX_HANDLES         16

enum { HANDLE_FREE, HANDLE_SEM, HANDLE_MBOX };

static struct {
    void      *object;
    program_t *program;     /* Creator, or NULL for a kernel thread */
    uint8_t    kind;
} handles[MAX_HANDLES];

/* Hand out a new object; frees it and returns false if the table is full */
static bool handle_add(void *object, uint8_t kind)
{
    uint8_t i;

    for (i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].kind == HANDLE_FREE) {
            handles[i].object  = object;
            handles[i].program = caller_program();
            handles[i].kind    = kind;
            return true;
        }
    }
    kfree(object);
    return false;
}

/* The table entry for a handle of the given kind, or -1 */
static int handle_find(uint16_t value, uint8_t kind)
{
    uint8_t i;

    for (i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].kind == kind && (uint16_t)handles[i].object == value) {
            return i;
        }
    }
    return -1;
}

static bool idle_object(const void *object, uint8_t kind)
{
    if (kind == HANDLE_SEM) {
        return ((const semaphore_t *)object)->wait_count == 0;
    }
    return ((const mailbox_t *)object)->items.wait_count == 0 &&
           ((const mailbox_t *)object)->space.wait_count == 0;
}

void syscall_program_ended(program_t *program)
{
    uint8_t i;

    for (i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].kind != HANDLE_FREE && handles[i].program == program) {
            if (idle_object(handles[i].object, handles[i].kind)) {
                kfree(handles[i].object);
                handles[i].kind = HANDLE_FREE;
            } else {
                handles[i].program = NULL;  /* Still in use by a kernel thread */
            }
        }
    }
}

/* ── Far memory ─────────────────────────────── */

/* May the caller free this far block? Only blocks its own program allocated. */
static bool caller_may_free(uint16_t segment)
{
    uint8_t owner;
    tcb_t *tcb;

    if (!far_owner(segment, &owner) || owner == FAR_NO_OWNER) {
        return false;               /* Not a block, or the kernel's own */
    }
    if (owner == (uint8_t)thread_current_tid()) {
        return true;
    }
    /* Another thread of the same program allocated it */
    tcb = thread_get_tcb(owner);
    return tcb && tcb->active && tcb->program && tcb->program == caller_program();
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
    bool refused = false;   /* An argument failed its check */
    char name[NAME_MAX_LENGTH];
    int tid;
    int handle;
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
            uint16_t room = caller_room(frame, frame->bx);
            uint16_t i;

            if (room == 0) {
                refused = true;
                break;
            }
            for (i = 0; i < PUTS_MAX_LENGTH && i < room && text[i]; i++) {
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

        if (caller->program) {
            /* A program starts threads only in itself, at an address in its code */
            if (frame->cs != caller->program->code_segment ||
                frame->bx >= caller->program->text_size) {
                refused = true;
                break;
            }
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
        if (obj && handle_add(obj, HANDLE_SEM)) {
            sem_init((semaphore_t *)obj, (int16_t)frame->bx);
            frame->ax = (uint16_t)obj;
        } else {
            error = true;
        }
        break;

    case SYS_SEM_WAIT:
        if (handle_find(frame->bx, HANDLE_SEM) >= 0) {
            sem_wait((semaphore_t *)frame->bx);
        } else {
            refused = true;
        }
        break;

    case SYS_SEM_SIGNAL:
        if (handle_find(frame->bx, HANDLE_SEM) >= 0) {
            sem_signal((semaphore_t *)frame->bx);
        } else {
            refused = true;
        }
        break;

    case SYS_MBOX_CREATE:
        obj = kmalloc(sizeof(mailbox_t));
        if (obj && handle_add(obj, HANDLE_MBOX)) {
            mbox_init((mailbox_t *)obj);
            frame->ax = (uint16_t)obj;
        } else {
            error = true;
        }
        break;

    case SYS_MBOX_SEND:
        /* BX = handle, CX = message */
        if (handle_find(frame->bx, HANDLE_MBOX) >= 0) {
            mbox_send((mailbox_t *)frame->bx, frame->cx);
        } else {
            refused = true;
        }
        break;

    case SYS_MBOX_RECV:
        /* BX = handle */
        if (handle_find(frame->bx, HANDLE_MBOX) >= 0) {
            frame->ax = mbox_recv((mailbox_t *)frame->bx);
        } else {
            refused = true;
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
        if (caller_may_free(frame->bx)) {
            far_free(frame->bx);
        } else {
            refused = true;
        }
        break;

    case SYS_SEM_DESTROY:
    case SYS_MBOX_DESTROY:
        /* Fails while threads wait on it: they would never wake */
        handle = handle_find(frame->bx, func == SYS_SEM_DESTROY ? HANDLE_SEM : HANDLE_MBOX);
        if (handle < 0) {
            refused = true;
        } else if (idle_object(handles[handle].object, handles[handle].kind)) {
            kfree(handles[handle].object);
            handles[handle].kind = HANDLE_FREE;
        } else {
            error = true;
        }
        break;

    case SYS_OPEN:
        /* BX = offset of the file name */
        if (!copy_name(frame, frame->bx, name)) {
            refused = true;
            break;
        }
        tid = file_open(name);
        if (tid < 0) {
            error = true;
        }
        frame->ax = (uint16_t)tid;
        break;

    case SYS_READ:
        /* BX = handle, CX = length, DX = offset of the buffer */
        if (!caller_owns(frame, frame->dx, frame->cx)) {
            refused = true;
        } else if (file_valid(frame->bx)) {
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
        if (!copy_name(frame, frame->bx, name)) {
            refused = true;
            break;
        }
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

    if (refused) {
        telemetry_thread_fault((uint8_t)thread_current_tid(), TEL_TFAULT_BAD_ARGUMENT, func);
        error = true;
    }

    if (error) {
        frame->flags |= FLAGS_CF;
        frame->ax = SYS_ERROR;
    }

    return resched;
}
