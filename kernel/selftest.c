/* SPDX-License-Identifier: MIT */
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
#include "fat12.h"
#include "exec.h"
#include "keyboard.h"

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

    /* Blocks tagged with an owner are reclaimed in one call; others are left alone */
    a = far_alloc_owned(8, 200);
    b = far_alloc_owned(8, 200);
    p = (uint8_t __far *)MK_FP(far_alloc(8), 0);
    p[0] = 0x77;
    far_free_owned(200);
    /* Both owned blocks are free again and merged: 8 + 8 + one header fits at a */
    expect(a != 0 && b == a + 9 && far_alloc(17) == a,
           "far: an owner's blocks are reclaimed together");
    expect(p[0] == 0x77, "far: a block with no owner is left alone");
    far_free(a);
    far_free((uint16_t)((uint32_t)p >> 16));
    expect(far_free_paras() == before, "far: everything is free again");

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
    expect((uint16_t)sys_call(SYS_AX(SYS_SEM_DESTROY, 0), sem, 0, 0) != SYS_ERROR,
           "syscall: sem_destroy");
    expect((uint16_t)sys_call(SYS_AX(SYS_SEM_DESTROY, 0), 0, 0, 0) == SYS_ERROR,
           "syscall: sem_destroy rejects a null handle");

    mbox = (uint16_t)sys_call(SYS_AX(SYS_MBOX_CREATE, 0), 0, 0, 0);
    expect(mbox != SYS_ERROR, "syscall: mbox_create");
    sys_call(SYS_AX(SYS_MBOX_SEND, 0), mbox, 0x1234, 0);
    expect((uint16_t)sys_call(SYS_AX(SYS_MBOX_RECV, 0), mbox, 0, 0) == 0x1234,
           "syscall: mbox_send and mbox_recv");
    expect((uint16_t)sys_call(SYS_AX(SYS_MBOX_DESTROY, 0), mbox, 0, 0) != SYS_ERROR,
           "syscall: mbox_destroy");
}

/* ── Keyboard layouts ───────────────────────── */

static void test_keymaps(void)
{
    char saved[3];
    const char *current = kb_keymap_name();

    saved[0] = current[0];
    saved[1] = current[1];
    saved[2] = '\0';

    expect(kb_set_keymap("us"), "keymap: us exists");
    expect(kb_translate(0x15, false, false) == 'y' && kb_translate(0x15, true, false) == 'Y',
           "keymap us: Y key");
    expect(kb_translate(0x03, true, false) == '@', "keymap us: shift-2 is @");
    expect(kb_translate(0x10, false, true) == 0, "keymap us: no AltGr level");

    expect(kb_set_keymap("uk"), "keymap: uk exists");
    expect(kb_translate(0x03, true, false) == '"' && kb_translate(0x28, true, false) == '@',
           "keymap uk: quote and @ are swapped");
    expect(kb_translate(0x1E, false, false) == 'a', "keymap uk: letters fall back to us");

    expect(kb_set_keymap("de"), "keymap: de exists");
    expect(kb_translate(0x15, false, false) == 'z' && kb_translate(0x2C, false, false) == 'y',
           "keymap de: Y and Z are swapped");
    expect(kb_translate(0x10, false, true) == '@', "keymap de: AltGr-Q is @");
    expect(kb_translate(0x1A, false, false) == 0x81, "keymap de: u-umlaut in code page 437");

    expect(kb_set_keymap("fr"), "keymap: fr exists");
    expect(kb_translate(0x10, false, false) == 'a' && kb_translate(0x1E, false, false) == 'q',
           "keymap fr: A and Q are swapped");
    expect(kb_translate(0x02, false, false) == '&' && kb_translate(0x02, true, false) == '1',
           "keymap fr: digits need shift");
    expect(kb_translate(0x0B, false, true) == '@', "keymap fr: AltGr-0 is @");

    expect(!kb_set_keymap("xx"), "keymap: unknown name is rejected");
    expect(kb_translate(0x7F, false, false) == 0, "keymap: out-of-range scan code gives nothing");

    kb_set_keymap(saved);
}

/* ── File system and loader ─────────────────── */

static void test_files(void)
{
    static const char readme[] = "README.TXT";
    static const char missing[] = "MISSING.BIN";
    fat_file_t file;
    uint8_t buffer[8];
    uint8_t error = EXEC_OK;
    uint16_t handle;

    if (!fat_mounted()) {
        con_println("  (no file system: file tests skipped)");
        return;
    }

    expect(fat_open("readme.txt", &file), "fat: open is case-insensitive");
    expect(fat_read(&file, buffer, 7) == 7 && buffer[0] == 'P' && buffer[6] == 'x',
           "fat: read returns the file's bytes");
    file.position = file.size;
    expect(fat_read(&file, buffer, 7) == 0, "fat: read at end of file returns nothing");
    expect(!fat_open("NOPE.TXT", &file), "fat: missing file is not found");

    handle = (uint16_t)sys_call(SYS_AX(SYS_OPEN, 0), (uint16_t)readme, 0, 0);
    expect(handle != SYS_ERROR, "syscall: open");
    expect((uint16_t)sys_call(SYS_AX(SYS_READ, 0), handle, 4, (uint16_t)buffer) == 4 &&
           buffer[0] == 'P' && buffer[3] == 'e', "syscall: read");
    sys_call(SYS_AX(SYS_CLOSE, 0), handle, 0, 0);
    expect((uint16_t)sys_call(SYS_AX(SYS_READ, 0), handle, 4, (uint16_t)buffer) == SYS_ERROR,
           "syscall: read on a closed handle fails");
    expect((uint16_t)sys_call(SYS_AX(SYS_OPEN, 0), (uint16_t)missing, 0, 0) == SYS_ERROR,
           "syscall: open of a missing file fails");

    expect(exec_program("MISSING.BIN", &error) < 0 && error == EXEC_NOT_FOUND,
           "exec: missing program is reported");
    expect(exec_program("README.TXT", &error) < 0 && error == EXEC_BAD_FORMAT,
           "exec: a file that is not a program is rejected");
    expect((uint16_t)sys_call(SYS_AX(SYS_EXEC, 0), (uint16_t)missing, 0, 0) == SYS_ERROR,
           "syscall: exec of a missing program fails");
}

/* ── Entry point ────────────────────────────── */

uint16_t selftest_run(void)
{
    uint16_t heap_before = mem_free();
    uint16_t far_before = far_free_paras();

    passed = 0;
    failed = 0;

    test_heap();
    test_far();
    test_sync();
    test_mailbox();
    test_sleep();
    test_syscalls();
    test_keymaps();
    test_files();

    expect(mem_free() == heap_before, "selftest: no heap leak");
    expect(far_free_paras() == far_before, "selftest: no far memory leak");

    con_print("selftest: ");
    con_print_dec(passed);
    con_print(" passed, ");
    con_print_dec(failed);
    con_println(" failed");

    return failed;
}
