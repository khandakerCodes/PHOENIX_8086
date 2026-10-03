/*
 * Phoenix-8086 — Kernel Self-Test
 *
 * Unit tests for kernel subsystems that run on the target itself,
 * from the shell's "selftest" command. They exercise the real code
 * with the real 16-bit pointers and segments, which a host build
 * could not. Must be called from a thread (some tests sleep).
 */

#include "selftest.h"
#include "console.h"
#include "memory.h"
#include "sync.h"
#include "ipc.h"
#include "thread.h"
#include "interrupts.h"
#include "syscall.h"
#include "hal.h"

static uint16_t passed;
static uint16_t failed;

static void expect(bool ok, const char *name)
{
    if (ok) {
        passed++;
    } else {
        failed++;
        con_print("  FAILED: ");
        con_println(name);
    }
}

/* ── Near heap ──────────────────────────────── */

static void test_heap(void)
{
    uint16_t before = mem_free();
    uint8_t *a = kmalloc(100);
    uint8_t *b = kmalloc(200);
    uint8_t *c = kmalloc(50);

    expect(a && b && c, "heap: three allocations succeed");
    expect(a != b && b != c && a != c, "heap: blocks are distinct");
    expect(mem_free() < before, "heap: free space shrinks");

    /* Free out of order so both coalescing directions are used */
    kfree(b);
    kfree(a);
    kfree(c);
    expect(mem_free() == before, "heap: free space fully restored");

    a = kmalloc(before - 64);
    expect(a != NULL, "heap: coalesced into one large block");
    kfree(a);

    expect(kmalloc(0xFFF0) == NULL, "heap: oversized request fails");
    expect(mem_free() == before, "heap: unchanged after failed request");
}

/* ── Far arena ──────────────────────────────── */

static void test_far(void)
{
    uint16_t before = far_free_paras();
    uint16_t a = far_alloc(64);
    uint16_t b = far_alloc(128);
    uint8_t __far *p;

    expect(a != 0 && b != 0 && a != b, "far: two allocations succeed");
    expect(a >= FAR_ARENA_SEG && b >= a + 64, "far: blocks do not overlap");

    /* Write to the last byte of the first block, read it back */
    p = (uint8_t __far *)MK_FP(a, 0);
    p[64 * 16 - 1] = 0x5A;
    expect(p[64 * 16 - 1] == 0x5A, "far: memory is writable");

    far_free(a);
    far_free(b);
    expect(far_free_paras() == before, "far: free space fully restored");

    expect(far_alloc(0xFFFF) == 0, "far: oversized request fails");
    expect(far_alloc(0) == 0, "far: zero-size request fails");
}

/* ── Semaphore and mutex ────────────────────── */

static void test_sync(void)
{
    semaphore_t sem;
    mutex_t mutex;

    sem_init(&sem, 2);
    expect(sem_trywait(&sem), "sem: first take succeeds");
    expect(sem_trywait(&sem), "sem: second take succeeds");
    expect(!sem_trywait(&sem), "sem: third take would block");
    sem_signal(&sem);
    expect(sem_trywait(&sem), "sem: take after signal succeeds");

    mutex_init(&mutex);
    mutex_lock(&mutex);
    expect(mutex.owner == thread_current_tid(), "mutex: owner recorded");
    mutex_unlock(&mutex);
    expect(mutex.owner == -1, "mutex: released");
    mutex_lock(&mutex);     /* Must not block: the mutex is free again */
    mutex_unlock(&mutex);
    expect(mutex.sem.count == 1, "mutex: relock and release");
}

/* ── Mailbox ────────────────────────────────── */

static void test_mailbox(void)
{
    mailbox_t mbox;
    uint16_t msg = 0;
    uint16_t i;
    bool ok = true;

    mbox_init(&mbox);
    expect(!mbox_try_recv(&mbox, &msg), "mbox: empty receive fails");

    for (i = 0; i < MAILBOX_SIZE; i++) {
        ok = ok && mbox_try_send(&mbox, 100 + i);
    }
    expect(ok, "mbox: fills to capacity");
    expect(!mbox_try_send(&mbox, 999), "mbox: full send fails");

    ok = true;
    for (i = 0; i < MAILBOX_SIZE; i++) {
        ok = ok && mbox_try_recv(&mbox, &msg) && msg == 100 + i;
    }
    expect(ok, "mbox: messages arrive in order");
    expect(!mbox_has_msg(&mbox), "mbox: empty after draining");
    expect(mbox_broadcast(&mbox, 1) == 0, "mbox: broadcast with no receivers sends nothing");
}

/* ── Timer and sleep ────────────────────────── */

static void test_sleep(void)
{
    uint32_t start = irq_ticks();
    uint32_t elapsed;

    thread_sleep(5);
    elapsed = irq_ticks() - start;
    expect(elapsed >= 5 && elapsed <= 7, "sleep: 5 ticks takes 5-7 ticks");
}

/* ── System calls ───────────────────────────── */

static void test_syscalls(void)
{
    uint16_t seg;
    uint16_t sem;
    uint16_t mbox;

    expect((uint16_t)sys_call(SYS_AX(SYS_VERSION, 0), 0, 0, 0) == SYSCALL_ABI_VERSION,
           "syscall: version");
    expect((uint16_t)sys_call(SYS_AX(0x7F, 0), 0, 0, 0) == SYS_ERROR,
           "syscall: unknown call returns error");

    seg = (uint16_t)sys_call(SYS_AX(SYS_ALLOC, 0), 16, 0, 0);
    expect(seg != SYS_ERROR && seg >= FAR_ARENA_SEG, "syscall: alloc");
    sys_call(SYS_AX(SYS_FREE, 0), seg, 0, 0);

    sem = (uint16_t)sys_call(SYS_AX(SYS_SEM_CREATE, 0), 1, 0, 0);
    expect(sem != SYS_ERROR, "syscall: sem_create");
    sys_call(SYS_AX(SYS_SEM_WAIT, 0), sem, 0, 0);       /* count 1 → 0, no block */
    sys_call(SYS_AX(SYS_SEM_SIGNAL, 0), sem, 0, 0);
    expect(((semaphore_t *)sem)->count == 1, "syscall: sem_wait and sem_signal");
    kfree((void *)sem);

    mbox = (uint16_t)sys_call(SYS_AX(SYS_MBOX_CREATE, 0), 0, 0, 0);
    expect(mbox != SYS_ERROR, "syscall: mbox_create");
    sys_call(SYS_AX(SYS_MBOX_SEND, 0), mbox, 0x1234, 0);
    expect((uint16_t)sys_call(SYS_AX(SYS_MBOX_RECV, 0), mbox, 0, 0) == 0x1234,
           "syscall: mbox_send and mbox_recv");
    kfree((void *)mbox);
}

/* ── Entry point ────────────────────────────── */

uint16_t selftest_run(void)
{
    uint16_t heap_before = mem_free();

    passed = 0;
    failed = 0;

    test_heap();
    test_far();
    test_sync();
    test_mailbox();
    test_sleep();
    test_syscalls();

    expect(mem_free() == heap_before, "selftest: no heap leak");

    con_print("selftest: ");
    con_print_dec(passed);
    con_print(" passed, ");
    con_print_dec(failed);
    con_println(" failed");

    return failed;
}
