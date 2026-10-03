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

/*
 * The system call itself (sdk/lib/crt0.S). Two names for one routine:
 * px_syscall returns the 16-bit result in AX, px_syscall32 the 32-bit
 * result in DX:AX. Use the 16-bit one unless the call returns 32 bits:
 * ia16-elf-gcc 6.3 miscompiles the narrowing of a 32-bit result to a
 * byte at -Os.
 */
unsigned      px_syscall(unsigned ax, unsigned bx, unsigned cx, unsigned dx);
unsigned long px_syscall32(unsigned ax, unsigned bx, unsigned cx, unsigned dx);

#define PX_AX(func, al) (((unsigned)(func) << 8) | (unsigned char)(al))

#define PX_CALL(func, al, bx, cx, dx) \
    px_syscall(PX_AX(func, al), (unsigned)(bx), (unsigned)(cx), (unsigned)(dx))

/* Console */
static inline void px_putc(char c)              { PX_CALL(0x01, c, 0, 0, 0); }
static inline void px_puts(const char *s)       { PX_CALL(0x02, 0, s, 0, 0); }
static inline char px_getc(void)
{
    unsigned key = PX_CALL(0x03, 0, 0, 0, 0);

    return (char)(key & 0xFF);
}

/* Threads and time */
static inline unsigned px_version(void)         { return PX_CALL(0x00, 0, 0, 0, 0); }
static inline void px_exit(void)                { PX_CALL(0x05, 0, 0, 0, 0); }

/*
 * Start another thread in this program. It runs `function` and exits
 * when the function returns. The program's memory stays loaded until
 * its last thread has ended. Returns the thread ID.
 */
void px_thread_entry(void);
static inline unsigned px_thread_create(void (*function)(void), unsigned priority)
{
    return PX_CALL(0x04, 0, px_thread_entry, priority, function);
}
static inline void px_yield(void)               { PX_CALL(0x06, 0, 0, 0, 0); }
static inline void px_sleep(unsigned ticks)     { PX_CALL(0x07, 0, 0, ticks, 0); }
static inline unsigned long px_ticks(void)      { return px_syscall32(PX_AX(0x08, 0), 0, 0, 0); }

/* Semaphores and mailboxes (handles come from the create calls) */
static inline unsigned px_sem_create(int count) { return PX_CALL(0x09, 0, count, 0, 0); }
static inline void px_sem_wait(unsigned sem)    { PX_CALL(0x0A, 0, sem, 0, 0); }
static inline void px_sem_signal(unsigned sem)  { PX_CALL(0x0B, 0, sem, 0, 0); }
static inline unsigned px_sem_destroy(unsigned sem) { return PX_CALL(0x11, 0, sem, 0, 0); }
static inline unsigned px_mbox_create(void)     { return PX_CALL(0x0C, 0, 0, 0, 0); }
static inline void px_mbox_send(unsigned mbox, unsigned msg) { PX_CALL(0x0D, 0, mbox, msg, 0); }
static inline unsigned px_mbox_recv(unsigned mbox) { return PX_CALL(0x0E, 0, mbox, 0, 0); }
static inline unsigned px_mbox_destroy(unsigned mbox) { return PX_CALL(0x12, 0, mbox, 0, 0); }

/* Far memory, in 16-byte paragraphs; returns a segment */
static inline unsigned px_alloc(unsigned paragraphs) { return PX_CALL(0x0F, 0, paragraphs, 0, 0); }
static inline void px_free(unsigned segment)    { PX_CALL(0x10, 0, segment, 0, 0); }

/* Files on the boot disk (read-only) and programs */
static inline unsigned px_open(const char *name) { return PX_CALL(0x18, 0, name, 0, 0); }
static inline unsigned px_read(unsigned file, void *buffer, unsigned length)
{
    return PX_CALL(0x19, 0, file, length, buffer);
}
static inline void px_close(unsigned file)      { PX_CALL(0x1A, 0, file, 0, 0); }
static inline unsigned px_exec(const char *name) { return PX_CALL(0x1B, 0, name, 0, 0); }

#endif /* PHOENIX_SDK_H */
