/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK — System Call Interface for Programs
 *
 * Every function here is a thin wrapper around INT 80h; see
 * docs/syscalls.md for the calls themselves. A failed call returns
 * PX_ERROR.
 *
 * Programs are freestanding: there is no C library. int is 16 bits,
 * long is 32 bits, pointers are 16 bits.
 */

#ifndef PHOENIX_SDK_H
#define PHOENIX_SDK_H

#define PX_ERROR    0xFFFFu
#define PX_HZ       100         /* Timer ticks per second */

unsigned long px_syscall(unsigned ax, unsigned bx, unsigned cx, unsigned dx);

#define PX_CALL(func, al, bx, cx, dx) \
    px_syscall(((unsigned)(func) << 8) | (unsigned char)(al), (unsigned)(bx), (unsigned)(cx), (unsigned)(dx))

/* Console */
static inline void px_putc(char c)              { PX_CALL(0x01, c, 0, 0, 0); }
static inline void px_puts(const char *s)       { PX_CALL(0x02, 0, s, 0, 0); }
static inline char px_getc(void)                { return (char)PX_CALL(0x03, 0, 0, 0, 0); }

/* Threads and time */
static inline unsigned px_version(void)         { return (unsigned)PX_CALL(0x00, 0, 0, 0, 0); }
static inline void px_exit(void)                { PX_CALL(0x05, 0, 0, 0, 0); }

/*
 * Start another thread in this program. It runs `function` and exits
 * when the function returns. The program's memory stays loaded until
 * its last thread has ended. Returns the thread ID.
 */
void px_thread_entry(void);
static inline unsigned px_thread_create(void (*function)(void), unsigned priority)
{
    return (unsigned)PX_CALL(0x04, 0, px_thread_entry, priority, function);
}
static inline void px_yield(void)               { PX_CALL(0x06, 0, 0, 0, 0); }
static inline void px_sleep(unsigned ticks)     { PX_CALL(0x07, 0, 0, ticks, 0); }
static inline unsigned long px_ticks(void)      { return PX_CALL(0x08, 0, 0, 0, 0); }

/* Semaphores and mailboxes (handles come from the create calls) */
static inline unsigned px_sem_create(int count) { return (unsigned)PX_CALL(0x09, 0, count, 0, 0); }
static inline void px_sem_wait(unsigned sem)    { PX_CALL(0x0A, 0, sem, 0, 0); }
static inline void px_sem_signal(unsigned sem)  { PX_CALL(0x0B, 0, sem, 0, 0); }
static inline unsigned px_sem_destroy(unsigned sem) { return (unsigned)PX_CALL(0x11, 0, sem, 0, 0); }
static inline unsigned px_mbox_create(void)     { return (unsigned)PX_CALL(0x0C, 0, 0, 0, 0); }
static inline void px_mbox_send(unsigned mbox, unsigned msg) { PX_CALL(0x0D, 0, mbox, msg, 0); }
static inline unsigned px_mbox_recv(unsigned mbox) { return (unsigned)PX_CALL(0x0E, 0, mbox, 0, 0); }
static inline unsigned px_mbox_destroy(unsigned mbox) { return (unsigned)PX_CALL(0x12, 0, mbox, 0, 0); }

/* Far memory, in 16-byte paragraphs; returns a segment */
static inline unsigned px_alloc(unsigned paragraphs) { return (unsigned)PX_CALL(0x0F, 0, paragraphs, 0, 0); }
static inline void px_free(unsigned segment)    { PX_CALL(0x10, 0, segment, 0, 0); }

/* Files on the boot disk (read-only) and programs */
static inline unsigned px_open(const char *name) { return (unsigned)PX_CALL(0x18, 0, name, 0, 0); }
static inline unsigned px_read(unsigned file, void *buffer, unsigned length)
{
    return (unsigned)PX_CALL(0x19, 0, file, length, buffer);
}
static inline void px_close(unsigned file)      { PX_CALL(0x1A, 0, file, 0, 0); }
static inline unsigned px_exec(const char *name) { return (unsigned)PX_CALL(0x1B, 0, name, 0, 0); }

#endif /* PHOENIX_SDK_H */
