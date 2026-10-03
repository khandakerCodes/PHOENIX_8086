/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Keyboard Driver Header
 */

#ifndef PHOENIX_KEYBOARD_H
#define PHOENIX_KEYBOARD_H

#include "../include/types.h"

/* Keyboard controller ports */
#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64

/* Key buffer size */
#define KB_BUFFER_SIZE  64

/* Initialize the keyboard driver */
void kb_init(void);

/* Handle a raw scancode (called from ISR) */
void kb_handle_scancode(uint8_t scancode);

/* Add a character as if it had been typed (used for serial input) */
void kb_inject_char(char c);

/* Get a character from the keyboard buffer (blocking) */
char kb_getchar(void);

/* Check if a character is available (non-blocking); pair with kb_getchar */
bool kb_haschar(void);

/* ── Keyboard layouts ───────────────────────── */

/*
 * Character for a key in the current layout, or 0 if the key produces
 * none. `scancode` is a set 1 make code (0x00-0x58). Characters above
 * 0x7F are code page 437, the PC's text-mode character set.
 */
uint8_t kb_translate(uint8_t scancode, bool shift, bool altgr);

/* Select a layout by name ("us", "uk", "de", "fr"); false if unknown */
bool kb_set_keymap(const char *name);

/* Name of the current layout */
const char *kb_keymap_name(void);

/*
 * Enumerate the layouts: fills in the name and description of layout
 * `index` and returns true, or returns false past the end.
 */
bool kb_keymap_info(uint8_t index, const char **name, const char **description);

#endif /* PHOENIX_KEYBOARD_H */
