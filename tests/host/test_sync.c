/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Host tests for kernel/sync.c and kernel/ipc.c
 *
 * Semaphores, mutexes with priority inheritance, and mailboxes, run
 * unmodified on the simulated machine (machine.h), where blocking
 * really suspends a thread. These cover cases the in-kernel self-test
 * cannot set up cheaply: inheritance chains, the depth limit, several
 * mutexes held at once, and exact wake-up order.
 */

#include "machine.h"
#include "../../kernel/sync.h"
#include "../../kernel/ipc.h"
#include "../../kernel/thread.h"

/* The order events were logged in, exactly */
static bool log_is(const char *expected)
{
    const char *got = host_log_text();

    while (*expected && *got == *expected) {
        expected++;
        got++;
    }
    return *expected == '\0' && *got == '\0';
}

/* ── Semaphores ─────────────────────────────── */

static semaphore_t sem;

static void waiter_a(void) { sem_wait(&sem); host_log('a'); }
static void waiter_b(void) { sem_wait(&sem); host_log('b'); }
static void waiter_c(void) { sem_wait(&sem); host_log('c'); }

static void test_semaphore_counts(void)
{
    host_reset();
    sem_init(&sem, 2);
    CHECK(sem_trywait(&sem));
    CHECK(sem_trywait(&sem));
    CHECK(!sem_trywait(&sem));
    sem_signal(&sem);
    CHECK(sem.count == 1);
    CHECK(sem_trywait(&sem));
}

static void test_semaphore_wakes_oldest_first(void)
{
    int a, b, c;

    host_reset();
    sem_init(&sem, 0);
    a = host_spawn(waiter_a, 5, "a");
    b = host_spawn(waiter_b, 5, "b");
    c = host_spawn(waiter_c, 5, "c");
    host_run();

    CHECK(host_tcb(a)->state == THREAD_BLOCKED && host_tcb(a)->wait_sem == &sem);
    CHECK(sem.count == -3 && sem.wait_count == 3);
    CHECK(sem.wait_queue[0] == a && sem.wait_queue[1] == b && sem.wait_queue[2] == c);

    sem_signal(&sem);           /* oldest: a */
    sem_signal_newest(&sem);    /* newest: c */
    host_run();
    CHECK(host_tcb(a)->state == THREAD_TERMINATED && host_tcb(c)->state == THREAD_TERMINATED);
    CHECK(host_tcb(b)->state == THREAD_BLOCKED);

    sem_signal(&sem);
    host_run();
    CHECK(host_tcb(b)->state == THREAD_TERMINATED);
    CHECK(sem.count == 0 && sem.wait_count == 0);
}

static void test_semaphore_remove_waiter(void)
{
    int a, b;

    host_reset();
    sem_init(&sem, 0);
    a = host_spawn(waiter_a, 5, "a");
    b = host_spawn(waiter_b, 5, "b");
    host_run();

    /* What thread_terminate does for a killed waiter */
    sem_remove_waiter(&sem, a);
    host_tcb(a)->active = false;
    CHECK(sem.count == -1 && sem.wait_count == 1 && sem.wait_queue[0] == b);

    sem_signal(&sem);
    host_run();
    CHECK(host_tcb(b)->state == THREAD_TERMINATED);
    CHECK(sem.count == 0);
}

/* ── Priority inheritance ───────────────────── */

static mutex_t m1, m2, m3;
static semaphore_t go;          /* Holds an owner inside its critical section */

static void low_holds_m1(void)
{
    mutex_lock(&m1);
    host_log('L');
    sem_wait(&go);
    host_log('l');
    mutex_unlock(&m1);
}

static void medium_busy(void) { host_log('M'); }

static void high_wants_m1(void)
{
    host_log('H');
    mutex_lock(&m1);
    host_log('h');
    mutex_unlock(&m1);
}

static void test_inheritance_basic(void)
{
    int low, high;

    host_reset();
    mutex_init(&m1);
    sem_init(&go, 0);

    low = host_spawn(low_holds_m1, 2, "low");
    host_run();                         /* low takes m1 and waits on go */
    CHECK(m1.owner == low && host_tcb(low)->mutexes_held == 1);

    high = host_spawn(high_wants_m1, 8, "high");
    host_run();                         /* high blocks on m1 */
    CHECK(host_tcb(high)->state == THREAD_BLOCKED && host_tcb(high)->wait_mutex == &m1);
    CHECK(host_tcb(low)->inherited == 8 && host_tcb(low)->eff_priority == 8);

    host_spawn(medium_busy, 6, "medium");
    sem_signal(&go);                    /* low (at 8) now outranks medium (6) */
    host_run();

    /* low finishes its critical section, high gets m1, and only then medium runs */
    CHECK(log_is("LHlhM"));
    CHECK(host_tcb(high)->wait_mutex == NULL);
    CHECK(m1.owner == -1 && m1.sem.count == 1);
    (void)high;
}

/* A chain: C waits for m2, held by B, which waits for m1, held by A */
static void a_holds_m1(void) { mutex_lock(&m1); sem_wait(&go); mutex_unlock(&m1); }
static void b_holds_m2_wants_m1(void) { mutex_lock(&m2); mutex_lock(&m1); mutex_unlock(&m1); mutex_unlock(&m2); }
static void c_wants_m2(void) { mutex_lock(&m2); mutex_unlock(&m2); }

static void test_inheritance_follows_a_chain(void)
{
    int a, b, c;

    host_reset();
    mutex_init(&m1);
    mutex_init(&m2);
    sem_init(&go, 0);

    a = host_spawn(a_holds_m1, 1, "a");
    host_run();
    b = host_spawn(b_holds_m2_wants_m1, 3, "b");
    host_run();
    CHECK(host_tcb(a)->inherited == 3);

    c = host_spawn(c_wants_m2, 9, "c");
    host_run();
    CHECK(host_tcb(b)->inherited == 9);
    CHECK(host_tcb(a)->inherited == 9 && host_tcb(a)->eff_priority == 9);

    sem_signal(&go);
    host_run();
    CHECK(host_tcb(c)->state == THREAD_TERMINATED);
    CHECK(m1.owner == -1 && m2.owner == -1);
    (void)c;
}

/*
 * Six threads in a line: each holds one mutex and waits for the one
 * below it. A priority-9 thread then waits on the top of the line.
 * Inheritance goes INHERIT_DEPTH (4) owners down and stops.
 */
static mutex_t chain[6];
static int chain_next;

static void chain_link(void)
{
    int me = chain_next++;

    mutex_lock(&chain[me]);
    if (me == 0) {
        sem_wait(&go);
    } else {
        mutex_lock(&chain[me - 1]);
        mutex_unlock(&chain[me - 1]);
    }
    mutex_unlock(&chain[me]);
}

static void top_of_chain(void)
{
    mutex_lock(&chain[5]);
    mutex_unlock(&chain[5]);
}

static void test_inheritance_depth_is_limited(void)
{
    int link[6];
    int i;

    host_reset();
    sem_init(&go, 0);
    chain_next = 0;
    for (i = 0; i < 6; i++) {
        mutex_init(&chain[i]);
        link[i] = host_spawn(chain_link, 1, "link");
        host_run();
    }
    host_spawn(top_of_chain, 9, "top");
    host_run();

    for (i = 5; i >= 2; i--) {
        CHECK(host_tcb(link[i])->inherited == 9);  /* four owners down */
    }
    CHECK(host_tcb(link[1])->inherited == 1);      /* beyond the limit: only the */
    CHECK(host_tcb(link[0])->inherited == 1);      /* links' own priority-1 loans */

    sem_signal(&go);
    host_run();
    for (i = 0; i < 6; i++) {
        CHECK(chain[i].owner == -1 && host_tcb(link[i])->state == THREAD_TERMINATED);
    }
}

/* Holding two mutexes: the loan lasts until the last one is released */
static void holder_of_two(void)
{
    mutex_lock(&m1);
    mutex_lock(&m2);
    sem_wait(&go);
    mutex_unlock(&m2);
    host_log(host_tcb(thread_current_tid())->eff_priority == 8 ? 'K' : 'k');
    mutex_unlock(&m1);
    host_log(host_tcb(thread_current_tid())->eff_priority == 2 ? 'R' : 'r');
}

static void test_inheritance_lasts_until_last_release(void)
{
    host_reset();
    mutex_init(&m1);
    mutex_init(&m2);
    sem_init(&go, 0);

    host_spawn(holder_of_two, 2, "holder");
    host_run();
    host_spawn(high_wants_m1, 8, "high");
    host_run();
    sem_signal(&go);
    host_run();
    /* K: still at 8 after releasing m2; then high runs (H..h); R: back to 2 */
    CHECK(log_is("HKhR"));
}

/* Locking a free mutex lends nothing */
static void lone_locker(void)
{
    mutex_lock(&m3);
    mutex_unlock(&m3);
}

static void test_no_inheritance_without_contention(void)
{
    int t;

    host_reset();
    mutex_init(&m3);
    t = host_spawn(lone_locker, 4, "lone");
    host_run();
    CHECK(host_tcb(t)->inherited == 0 && host_tcb(t)->mutexes_held == 0);
}

/* ── Mailboxes ──────────────────────────────── */

static mailbox_t box;
static uint16_t received[40];
static int received_count;

static void producer(void)
{
    uint16_t i;

    for (i = 1; i <= 20; i++) {
        mbox_send(&box, i);     /* blocks when the mailbox is full */
    }
}

static void consumer(void)
{
    int i;

    for (i = 0; i < 20; i++) {
        received[received_count++] = mbox_recv(&box);
    }
}

static void test_mailbox_blocks_both_ways(void)
{
    int p, i;
    bool in_order = true;

    host_reset();
    mbox_init(&box);
    received_count = 0;

    p = host_spawn(producer, 5, "producer");
    host_run();
    CHECK(host_tcb(p)->state == THREAD_BLOCKED);    /* full after MAILBOX_SIZE messages */
    CHECK(box.count == MAILBOX_SIZE);

    host_spawn(consumer, 5, "consumer");
    host_run();
    CHECK(received_count == 20);
    for (i = 0; i < 20; i++) {
        in_order = in_order && received[i] == i + 1;
    }
    CHECK(in_order);
    CHECK(box.count == 0 && host_tcb(p)->state == THREAD_TERMINATED);
}

static void one_receiver(void) { received[received_count++] = mbox_recv(&box); }

static void test_mailbox_broadcast(void)
{
    host_reset();
    mbox_init(&box);
    received_count = 0;

    host_spawn(one_receiver, 5, "r1");
    host_spawn(one_receiver, 5, "r2");
    host_spawn(one_receiver, 5, "r3");
    host_run();
    CHECK(mbox_broadcast(&box, 77) == 3);
    host_run();
    CHECK(received_count == 3 && received[0] == 77 && received[2] == 77);
    CHECK(mbox_broadcast(&box, 1) == 0);
}

int main(void)
{
    test_semaphore_counts();
    test_semaphore_wakes_oldest_first();
    test_semaphore_remove_waiter();
    test_inheritance_basic();
    test_inheritance_follows_a_chain();
    test_inheritance_depth_is_limited();
    test_inheritance_lasts_until_last_release();
    test_no_inheritance_without_contention();
    test_mailbox_blocks_both_ways();
    test_mailbox_broadcast();
    return host_summary("sync");
}
