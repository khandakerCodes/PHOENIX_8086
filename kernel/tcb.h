/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Task Control Block Header
 *
 * Defines the TCB structure that holds per-thread state and the
 * register frame that every interrupt pushes on a thread's stack.
 */

#ifndef PHOENIX_TCB_H
#define PHOENIX_TCB_H

#include "../include/types.h"

/*
 * Saved register frame
 *
 * A thread's registers are saved on its own stack by the ISR stubs
 * in isr.S. The layout below is that push order, lowest address
 * first; the last three words are what the CPU pushes for an
 * interrupt and what IRET pops. The TCB only stores the stack
 * pointer that points at this frame.
 */
typedef struct {
    uint16_t es;
    uint16_t ds;
    uint16_t di;
    uint16_t si;
    uint16_t bp;
    uint16_t bx;
    uint16_t dx;
    uint16_t cx;
    uint16_t ax;
    uint16_t ip;
    uint16_t cs;
    uint16_t flags;
} frame_t;

/*
 * Saved context
 *
 * What an ISR stub leaves on a thread's kernel stack, lowest address
 * first, and what the TCB's saved stack pointer points at.
 *
 * A thread interrupted while running kernel code already was on its
 * kernel stack: the frame was pushed right there. A thread interrupted
 * while running a program was on the program's stack, in the program's
 * own data segment; the stub copies the frame to the kernel stack and
 * notes where the program's stack was, so that the handlers (which are
 * C code and need SS = DS = the kernel data segment) can run. On the
 * way back the frame is copied to the program's stack again and the
 * IRET happens there.
 */
typedef struct {
    uint16_t from_program;  /* 0: interrupted kernel code; 1: interrupted a program */
    frame_t  frame;
} context_t;

typedef struct {
    context_t context;      /* from_program = 1 */
    uint16_t  user_sp;      /* Where the frame sits on the program's stack */
    uint16_t  user_ss;      /* The program's data (and stack) segment */
} user_context_t;

#define CONTEXT(sp)         ((context_t *)(sp))
#define CONTEXT_FRAME(sp)   (&CONTEXT(sp)->frame)

/* The stack pointer the thread resumes with once its frame has been popped */
#define CONTEXT_RESUME_SP(sp) \
    (CONTEXT(sp)->from_program \
        ? ((user_context_t *)(sp))->user_sp + sizeof(frame_t) \
        : (uint16_t)CONTEXT_FRAME(sp) + sizeof(frame_t))

/*
 * A loaded program. Its code and its data each have a segment of their
 * own in far memory; the data segment also holds the stacks of the
 * program's threads. Every thread running in the program holds a
 * reference, and the memory is freed when the last of them ends.
 */
#define PROG_MAX_THREADS    4       /* Threads one program can have at once */
#define PROG_STACK_SIZE     2048    /* Stack for each of them, in the program's data segment */
#define PROG_DATA_START     16      /* Offset of the program's data; below it is unused, so NULL is never valid */

typedef struct program {
    uint16_t code_segment;
    uint16_t data_segment;
    uint16_t text_size;     /* Bytes of code; entry points must lie below this */
    uint16_t data_limit;    /* Bytes of the data segment in use (data, bss and stacks) */
    uint16_t stack_area;    /* Offset of the first thread stack in the data segment */
    uint8_t  threads;       /* Threads currently running in this program */
    uint8_t  stacks_in_use; /* Bit n set: stack n belongs to a thread */
} program_t;

/* Offset of the lowest byte of stack n in the program's data segment */
#define PROG_STACK_BOTTOM(program, n) \
    ((program)->stack_area + (uint16_t)(n) * PROG_STACK_SIZE)

/*
 * A program thread is stopped when its stack pointer comes this close
 * to the bottom of its stack. Only interrupt frames (24 bytes) are ever
 * pushed on a program's stack by the kernel, so this can be smaller
 * than the kernel's own red zone.
 */
#define PROG_STACK_RED_ZONE 64

/* FLAGS bits used by the kernel */
#define FLAGS_CF            0x0001
#define FLAGS_IF            0x0200
#define FLAGS_INITIAL       0x0202  /* IF set, reserved bit 1 set */

/*
 * Task Control Block
 *
 *   +0x00  SP   Saved stack pointer (points at a context_t while not running)
 *   +0x02  SS   Stack segment (always the kernel data segment)
 *
 * The remaining fields are used from C only.
 */
typedef struct {
    uint16_t sp;            /* +0x00 Saved stack pointer */
    uint16_t ss;            /* +0x02 Stack segment */

    /* Scheduling */
    uint8_t  state;         /* Thread state */
    uint8_t  priority;      /* Base priority (0 = lowest) */
    uint8_t  eff_priority;  /* Effective priority (raised by aging) */
    uint8_t  slice_left;    /* Ticks left in the current time slice */
    uint8_t  wait_ticks;    /* Ticks spent READY since the last aging step */
    uint32_t sleep_until;   /* Wake tick for SLEEPING threads */
    void    *wait_sem;      /* Semaphore this thread is blocked on, or NULL */
    void    *wait_mutex;    /* Mutex it is waiting for, or NULL (priority inheritance) */
    uint8_t  inherited;     /* Priority lent by a thread waiting on a mutex it holds, 0 if none */
    uint8_t  mutexes_held;  /* Mutexes it owns; inheritance ends when this drops to 0 */
    bool     wait_timed;    /* The wait ends at sleep_until even without a signal */
    bool     wait_timed_out;/* Set when that happened */

    /* Identity and accounting */
    uint16_t tid;           /* Thread ID */
    uint16_t stack_base;    /* Lowest address of the kernel stack (guard word lives here) */
    uint16_t stack_size;    /* Size of the stack in bytes */
    uint32_t cpu_ticks;     /* CPU time consumed (in timer ticks) */
    uint32_t last_scheduled;/* Tick when last scheduled */
    /* Loaded program this thread runs in, or NULL for a kernel thread */
    struct program *program;
    uint8_t  program_stack; /* Which of the program's stacks this thread uses */

    char     name[12];      /* Human-readable thread name */

    bool     active;        /* Is this TCB slot in use? */
} tcb_t;

/* Stack guard magic value */
#define STACK_GUARD_VALUE   0xAAAA

/*
 * Every new stack is filled with this word, so the part a thread has
 * never touched can be told from the part it has used. It differs from
 * the guard so the guard is never mistaken for unused space.
 */
#define STACK_FILL_WORD     0xA5A5

#endif /* PHOENIX_TCB_H */
