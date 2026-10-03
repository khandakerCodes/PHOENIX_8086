/*
 * Phoenix-8086 — IPC (Inter-Process Communication) Header
 */

#ifndef PHOENIX_IPC_H
#define PHOENIX_IPC_H

#include "../include/types.h"
#include "sync.h"

/* ── Mailbox ────────────────────────────────── */
typedef struct {
    uint16_t    data[MAILBOX_SIZE];
    uint8_t     head;
    uint8_t     tail;
    uint8_t     count;
    semaphore_t items;  /* Counts messages; receivers block on it */
    semaphore_t space;  /* Counts free slots; senders block on it */
} mailbox_t;

/* Initialize a mailbox */
void mbox_init(mailbox_t *m);

/* Send a message (blocks if full) */
void mbox_send(mailbox_t *m, uint16_t msg);

/* Receive a message (blocks if empty) */
uint16_t mbox_recv(mailbox_t *m);

/* Send without blocking; returns false if the mailbox is full */
bool mbox_try_send(mailbox_t *m, uint16_t msg);

/* Receive without blocking; returns false if the mailbox is empty */
bool mbox_try_recv(mailbox_t *m, uint16_t *msg);

/*
 * Deliver one copy of msg to every thread currently blocked in
 * mbox_recv on this mailbox. Returns the number of copies sent.
 */
uint8_t mbox_broadcast(mailbox_t *m, uint16_t msg);

/* Check if mailbox has messages */
bool mbox_has_msg(mailbox_t *m);

#endif /* PHOENIX_IPC_H */
