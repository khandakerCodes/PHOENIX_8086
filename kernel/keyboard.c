/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Keyboard Driver Implementation
 *
 * Reads raw scan codes from port 0x60, translates a minimal
 * set to ASCII, and buffers characters in a ring buffer for
 * consumption by the shell or other kernel consumers.
 */

#include "keyboard.h"
#include "console.h"
#include "sync.h"
#include "hal.h"

/* ── Ring buffer ────────────────────────────── */
static char    kb_buffer[KB_BUFFER_SIZE];
static uint8_t kb_head = 0;
static uint8_t kb_tail = 0;
static uint8_t kb_count = 0;

/* Counts buffered characters; kb_getchar blocks on it */
static semaphore_t kb_sem;

/* Shift key state */
static bool kb_shift = false;

/* ── Scan code to ASCII table (US layout, set 1) ── */

/* Unshifted key map (index = scan code) */
static const char scancode_map[128] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',    /* 00-07 */
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',   /* 08-0F */
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',    /* 10-17 */
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',     /* 18-1F */
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',    /* 20-27 */
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',    /* 28-2F */
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',    /* 30-37 */
    0,    ' ',  0,    0,    0,    0,    0,    0,       /* 38-3F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 40-47 */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 48-4F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 50-57 */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 58-5F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 60-67 */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 68-6F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 70-77 */
    0,    0,    0,    0,    0,    0,    0,    0        /* 78-7F */
};

/* Shifted key map */
static const char scancode_map_shift[128] = {
    0,    27,   '!',  '@',  '#',  '$',  '%',  '^',    /* 00-07 */
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',   /* 08-0F */
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',    /* 10-17 */
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',     /* 18-1F */
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',    /* 20-27 */
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',    /* 28-2F */
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',    /* 30-37 */
    0,    ' ',  0,    0,    0,    0,    0,    0,       /* 38-3F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 40-47 */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 48-4F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 50-57 */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 58-5F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 60-67 */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 68-6F */
    0,    0,    0,    0,    0,    0,    0,    0,       /* 70-77 */
    0,    0,    0,    0,    0,    0,    0,    0        /* 78-7F */
};

/* Shift scan codes */
#define SC_LSHIFT_PRESS     0x2A
#define SC_RSHIFT_PRESS     0x36
#define SC_LSHIFT_RELEASE   0xAA
#define SC_RSHIFT_RELEASE   0xB6

/* ── Buffer operations ──────────────────────── */

/* Called from the keyboard ISR only (interrupts off) */
static void kb_buf_push(char c)
{
    if (kb_count < KB_BUFFER_SIZE) {
        kb_buffer[kb_head] = c;
        kb_head = (kb_head + 1) % KB_BUFFER_SIZE;
        kb_count++;
        sem_signal(&kb_sem);    /* Wake a thread blocked in kb_getchar */
    }
}

static char kb_buf_pop(void)
{
    uint16_t flags = hal_irq_save();
    char c = 0;

    if (kb_count > 0) {
        c = kb_buffer[kb_tail];
        kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
        kb_count--;
    }

    hal_irq_restore(flags);
    return c;
}

/* ── Public API ─────────────────────────────── */

void kb_init(void)
{
    kb_head = 0;
    kb_tail = 0;
    kb_count = 0;
    kb_shift = false;
    sem_init(&kb_sem, 0);
}

void kb_handle_scancode(uint8_t scancode)
{
    char c;

    /* Track shift key state */
    if (scancode == SC_LSHIFT_PRESS || scancode == SC_RSHIFT_PRESS) {
        kb_shift = true;
        return;
    }
    if (scancode == SC_LSHIFT_RELEASE || scancode == SC_RSHIFT_RELEASE) {
        kb_shift = false;
        return;
    }

    /* Ignore key releases (bit 7 set) */
    if (scancode & 0x80) {
        return;
    }

    /* Translate scan code to ASCII */
    if (kb_shift) {
        c = scancode_map_shift[scancode];
    } else {
        c = scancode_map[scancode];
    }

    /* Push to buffer if it's a valid character */
    if (c != 0) {
        kb_buf_push(c);
    }
}

char kb_getchar(void)
{
    /* Block until the keyboard ISR has buffered a character */
    sem_wait(&kb_sem);
    return kb_buf_pop();
}

bool kb_haschar(void)
{
    return kb_count > 0;
}
