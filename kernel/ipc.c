/*
 * Phoenix-8086 — IPC (Inter-Process Communication) Implementation
 *
 * Mailbox-based message passing between threads.
 * Threads can send and receive 16-bit messages through
 * a shared mailbox with blocking semantics: a sender blocks while
 * the mailbox is full, a receiver blocks while it is empty.
 */

#include "ipc.h"
#include "thread.h"
#include "interrupts.h"
#include "hal.h"

void mbox_init(mailbox_t *m)
{
    m->head = 0;
    m->tail = 0;
    m->count = 0;
    sem_init(&m->items, 0);
    sem_init(&m->space, MAILBOX_SIZE);
}

/* Queue operations; the caller already holds a slot or a message */

static void mbox_put(mailbox_t *m, uint16_t msg)
{
    uint16_t flags = hal_irq_save();

    m->data[m->head] = msg;
    m->head = (m->head + 1) % MAILBOX_SIZE;
    m->count++;

    hal_irq_restore(flags);
}

static uint16_t mbox_take(mailbox_t *m)
{
    uint16_t flags = hal_irq_save();
    uint16_t msg;

    msg = m->data[m->tail];
    m->tail = (m->tail + 1) % MAILBOX_SIZE;
    m->count--;

    hal_irq_restore(flags);
    return msg;
}

void mbox_send(mailbox_t *m, uint16_t msg)
{
    /* Block until space is available */
    sem_wait(&m->space);
    mbox_put(m, msg);
    sem_signal(&m->items);
}

uint16_t mbox_recv(mailbox_t *m)
{
    uint16_t msg;

    /* Block until a message is available */
    sem_wait(&m->items);
    msg = mbox_take(m);
    sem_signal(&m->space);

    return msg;
}

bool mbox_try_send(mailbox_t *m, uint16_t msg)
{
    if (!sem_trywait(&m->space)) {
        return false;
    }
    mbox_put(m, msg);
    sem_signal(&m->items);
    return true;
}

bool mbox_try_recv(mailbox_t *m, uint16_t *msg)
{
    if (!sem_trywait(&m->items)) {
        return false;
    }
    *msg = mbox_take(m);
    sem_signal(&m->space);
    return true;
}

uint8_t mbox_broadcast(mailbox_t *m, uint16_t msg)
{
    uint16_t flags = hal_irq_save();
    uint8_t receivers = m->items.wait_count;
    uint8_t sent = 0;

    /* Each successful send wakes exactly one of the waiting receivers */
    while (sent < receivers && mbox_try_send(m, msg)) {
        sent++;
    }

    hal_irq_restore(flags);
    return sent;
}

bool mbox_has_msg(mailbox_t *m)
{
    return m->count > 0;
}
