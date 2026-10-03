/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Keyboard Driver Implementation
 *
 * Reads raw scan codes (set 1) from port 0x60, translates them to
 * characters through the selected keyboard layout, and buffers the
 * characters in a ring buffer for the shell or other consumers.
 *
 * A layout is the US table plus a short list of the keys that differ,
 * so adding one means listing only its differences (see docs/labs).
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

/* Modifier state */
static bool kb_shift = false;
static bool kb_altgr = false;       /* Right Alt: third level on European layouts */
static bool kb_caps = false;
static bool kb_extended = false;    /* The previous byte was the 0xE0 prefix */

/* ── Scan codes ─────────────────────────────── */

#define SC_MAX              0x58    /* Highest make code in the tables */
#define SC_EXTENDED         0xE0    /* Prefix for the keys added by the 101-key keyboard */
#define SC_RELEASE          0x80    /* Set in the break (key up) code */

#define SC_LSHIFT           0x2A
#define SC_RSHIFT           0x36
#define SC_ALT              0x38    /* Right Alt (AltGr) when prefixed by 0xE0 */
#define SC_CAPS_LOCK        0x3A

/* ── US layout: the base every other layout builds on ── */

static const uint8_t us_normal[SC_MAX + 1] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',    /* 00-07 */
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',   /* 08-0F */
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',    /* 10-17 */
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',    /* 18-1F */
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',    /* 20-27 */
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',    /* 28-2F */
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',    /* 30-37 */
    0,    ' ',  0,    0,    0,    0,    0,    0,      /* 38-3F */
    0,    0,    0,    0,    0,    0,    0,    0,      /* 40-47 */
    0,    0,    0,    0,    0,    0,    0,    0,      /* 48-4F */
    0,    0,    0,    0,    0,    0,    '\\', 0,      /* 50-57 */
    0                                                 /* 58 */
};

static const uint8_t us_shift[SC_MAX + 1] = {
    0,    27,   '!',  '@',  '#',  '$',  '%',  '^',    /* 00-07 */
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',   /* 08-0F */
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',    /* 10-17 */
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',    /* 18-1F */
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',    /* 20-27 */
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',    /* 28-2F */
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',    /* 30-37 */
    0,    ' ',  0,    0,    0,    0,    0,    0,      /* 38-3F */
    0,    0,    0,    0,    0,    0,    0,    0,      /* 40-47 */
    0,    0,    0,    0,    0,    0,    0,    0,      /* 48-4F */
    0,    0,    0,    0,    0,    0,    '|',  0,      /* 50-57 */
    0                                                 /* 58 */
};

/* ── Layouts as differences from US ─────────── */

/* One key that differs from the US layout. 0 = the key produces nothing. */
typedef struct {
    uint8_t scancode;
    uint8_t normal;
    uint8_t shift;
    uint8_t altgr;
} key_override_t;

typedef struct {
    const char           *name;
    const char           *description;
    const key_override_t *keys;
    uint8_t               count;
} keymap_t;

/* Code page 437 characters used below */
#define CP_U_UMLAUT     0x81    /* ü */
#define CP_E_ACUTE      0x82    /* é */
#define CP_A_UMLAUT     0x84    /* ä */
#define CP_A_GRAVE      0x85    /* à */
#define CP_C_CEDILLA    0x87    /* ç */
#define CP_E_GRAVE      0x8A    /* è */
#define CP_A_UMLAUT_UP  0x8E    /* Ä */
#define CP_O_UMLAUT     0x94    /* ö */
#define CP_U_GRAVE      0x97    /* ù */
#define CP_O_UMLAUT_UP  0x99    /* Ö */
#define CP_U_UMLAUT_UP  0x9A    /* Ü */
#define CP_POUND        0x9C    /* £ */
#define CP_NOT          0xAA    /* ¬ */
#define CP_SHARP_S      0xE1    /* ß */
#define CP_MICRO        0xE6    /* µ */
#define CP_DEGREE       0xF8    /* ° */
#define CP_SQUARED      0xFD    /* ² */
#define CP_SECTION      0x15    /* § */

static const key_override_t uk_keys[] = {
    { 0x03, '2',  '"',      0 },
    { 0x04, '3',  CP_POUND, 0 },
    { 0x28, '\'', '@',      0 },
    { 0x29, '`',  CP_NOT,   '|' },
    { 0x2B, '#',  '~',      0 },
    { 0x56, '\\', '|',      0 },
};

static const key_override_t de_keys[] = {
    { 0x03, '2',  '"',  CP_SQUARED },
    { 0x04, '3',  CP_SECTION, 0 },
    { 0x07, '6',  '&',  0 },
    { 0x08, '7',  '/',  '{' },
    { 0x09, '8',  '(',  '[' },
    { 0x0A, '9',  ')',  ']' },
    { 0x0B, '0',  '=',  '}' },
    { 0x0C, CP_SHARP_S, '?', '\\' },
    { 0x0D, '\'', '`',  0 },
    { 0x10, 'q',  'Q',  '@' },
    { 0x15, 'z',  'Z',  0 },
    { 0x1A, CP_U_UMLAUT, CP_U_UMLAUT_UP, 0 },
    { 0x1B, '+',  '*',  '~' },
    { 0x27, CP_O_UMLAUT, CP_O_UMLAUT_UP, 0 },
    { 0x28, CP_A_UMLAUT, CP_A_UMLAUT_UP, 0 },
    { 0x29, '^',  CP_DEGREE, 0 },
    { 0x2B, '#',  '\'', 0 },
    { 0x2C, 'y',  'Y',  0 },
    { 0x32, 'm',  'M',  CP_MICRO },
    { 0x33, ',',  ';',  0 },
    { 0x34, '.',  ':',  0 },
    { 0x35, '-',  '_',  0 },
    { 0x56, '<',  '>',  '|' },
};

static const key_override_t fr_keys[] = {
    { 0x02, '&',  '1',  0 },
    { 0x03, CP_E_ACUTE, '2', '~' },
    { 0x04, '"',  '3',  '#' },
    { 0x05, '\'', '4',  '{' },
    { 0x06, '(',  '5',  '[' },
    { 0x07, '-',  '6',  '|' },
    { 0x08, CP_E_GRAVE, '7', '`' },
    { 0x09, '_',  '8',  '\\' },
    { 0x0A, CP_C_CEDILLA, '9', '^' },
    { 0x0B, CP_A_GRAVE, '0', '@' },
    { 0x0C, ')',  CP_DEGREE, ']' },
    { 0x0D, '=',  '+',  '}' },
    { 0x10, 'a',  'A',  0 },
    { 0x11, 'z',  'Z',  0 },
    { 0x1A, '^',  '"',  0 },
    { 0x1B, '$',  CP_POUND, 0 },
    { 0x1E, 'q',  'Q',  0 },
    { 0x27, 'm',  'M',  0 },
    { 0x28, CP_U_GRAVE, '%', 0 },
    { 0x29, CP_SQUARED, 0, 0 },
    { 0x2B, '*',  CP_MICRO, 0 },
    { 0x2C, 'w',  'W',  0 },
    { 0x32, ',',  '?',  0 },
    { 0x33, ';',  '.',  0 },
    { 0x34, ':',  '/',  0 },
    { 0x35, '!',  CP_SECTION, 0 },
    { 0x56, '<',  '>',  0 },
};

#define KEYS(table) table, sizeof(table) / sizeof(table[0])

static const keymap_t keymaps[] = {
    { "us", "United States (QWERTY)",  NULL, 0 },
    { "uk", "United Kingdom (QWERTY)", KEYS(uk_keys) },
    { "de", "German (QWERTZ)",         KEYS(de_keys) },
    { "fr", "French (AZERTY)",         KEYS(fr_keys) },
};

#define KEYMAP_COUNT (sizeof(keymaps) / sizeof(keymaps[0]))

static const keymap_t *current_keymap = &keymaps[0];

/* ── Translation ────────────────────────────── */

uint8_t kb_translate(uint8_t scancode, bool shift, bool altgr)
{
    const keymap_t *map = current_keymap;
    uint8_t i;

    if (scancode > SC_MAX) {
        return 0;
    }

    for (i = 0; i < map->count; i++) {
        if (map->keys[i].scancode == scancode) {
            if (altgr) return map->keys[i].altgr;
            return shift ? map->keys[i].shift : map->keys[i].normal;
        }
    }

    /* Not overridden: the US character. The US layout has no third level. */
    if (altgr) {
        return 0;
    }
    return shift ? us_shift[scancode] : us_normal[scancode];
}

static bool name_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool kb_set_keymap(const char *name)
{
    uint8_t i;

    for (i = 0; i < KEYMAP_COUNT; i++) {
        if (name_equal(name, keymaps[i].name)) {
            current_keymap = &keymaps[i];
            return true;
        }
    }
    return false;
}

const char *kb_keymap_name(void)
{
    return current_keymap->name;
}

bool kb_keymap_info(uint8_t index, const char **name, const char **description)
{
    if (index >= KEYMAP_COUNT) {
        return false;
    }
    *name = keymaps[index].name;
    *description = keymaps[index].description;
    return true;
}

/* ── Buffer operations ──────────────────────── */

/* Called with interrupts off */
static void kb_buf_push(char c)
{
    if (kb_count < KB_BUFFER_SIZE) {
        kb_buffer[kb_head] = c;
        kb_head = (kb_head + 1) % KB_BUFFER_SIZE;
        kb_count++;
        /*
         * Wake a thread blocked in kb_getchar. If several are waiting,
         * the key goes to the one that asked last: a program that calls
         * getc takes the keyboard from the shell until it stops asking.
         */
        sem_signal_newest(&kb_sem);
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
    kb_altgr = false;
    kb_caps = false;
    kb_extended = false;
    sem_init(&kb_sem, 0);
}

void kb_handle_scancode(uint8_t scancode)
{
    bool extended = kb_extended;
    bool released = (scancode & SC_RELEASE) != 0;
    uint8_t key = scancode & ~SC_RELEASE;
    uint8_t c;

    if (scancode == SC_EXTENDED) {
        kb_extended = true;
        return;
    }
    kb_extended = false;

    /* Modifiers */
    if (key == SC_LSHIFT || key == SC_RSHIFT) {
        /* Extended shift codes are padding sent around some keys; ignore them */
        if (!extended) {
            kb_shift = !released;
        }
        return;
    }
    if (key == SC_ALT) {
        if (extended) {
            kb_altgr = !released;
        }
        return;
    }
    if (key == SC_CAPS_LOCK) {
        if (!released) {
            kb_caps = !kb_caps;
        }
        return;
    }

    if (released) {
        return;
    }

    c = kb_translate(key, kb_shift, kb_altgr);

    /* Caps Lock swaps the case of unaccented letters */
    if (kb_caps) {
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        } else if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
    }

    if (c != 0) {
        kb_buf_push((char)c);
    }
}

void kb_inject_char(char c)
{
    uint16_t flags = hal_irq_save();

    kb_buf_push(c);

    hal_irq_restore(flags);
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
