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

/* Get a character from the keyboard buffer (blocking) */
char kb_getchar(void);

/* Check if a character is available (non-blocking); pair with kb_getchar */
bool kb_haschar(void);

#endif /* PHOENIX_KEYBOARD_H */
