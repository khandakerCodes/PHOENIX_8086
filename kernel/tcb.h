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

/* FLAGS bits used by the kernel */
#define FLAGS_CF            0x0001
#define FLAGS_IF            0x0200
#define FLAGS_INITIAL       0x0202  /* IF set, reserved bit 1 set */

/*
 * Task Control Block
 *
 *   +0x00  SP   Saved stack pointer (points at a frame_t while not running)
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

    /* Identity and accounting */
    uint16_t tid;           /* Thread ID */
    uint16_t stack_base;    /* Lowest address of the stack (guard word lives here) */
    uint16_t stack_size;    /* Size of the stack in bytes */
    uint32_t cpu_ticks;     /* CPU time consumed (in timer ticks) */
    uint32_t last_scheduled;/* Tick when last scheduled */

    char     name[12];      /* Human-readable thread name */

    bool     active;        /* Is this TCB slot in use? */
} tcb_t;

/* Stack guard magic value */
#define STACK_GUARD_VALUE   0xAAAA

#endif /* PHOENIX_TCB_H */
