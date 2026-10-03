/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Console Driver Header
 *
 * VGA text-mode console driver for direct framebuffer writes
 * at B800:0000 (physical 0xB8000).
 */

#ifndef PHOENIX_CONSOLE_H
#define PHOENIX_CONSOLE_H

#include "../include/types.h"

/* VGA text mode constants */
#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_MEMORY      0xB8000

/* VGA color attributes */
#define VGA_BLACK       0x0
#define VGA_BLUE        0x1
#define VGA_GREEN       0x2
#define VGA_CYAN        0x3
#define VGA_RED         0x4
#define VGA_MAGENTA     0x5
#define VGA_BROWN       0x6
#define VGA_LIGHT_GRAY  0x7
#define VGA_DARK_GRAY   0x8
#define VGA_LIGHT_BLUE  0x9
#define VGA_LIGHT_GREEN 0xA
#define VGA_LIGHT_CYAN  0xB
#define VGA_LIGHT_RED   0xC
#define VGA_PINK        0xD
#define VGA_YELLOW      0xE
#define VGA_WHITE       0xF

/* Build a VGA attribute byte from foreground and background colors */
#define VGA_COLOR(fg, bg)  (((bg) << 4) | (fg))

/* Default color: light gray on black */
#define VGA_DEFAULT_COLOR  VGA_COLOR(VGA_LIGHT_GRAY, VGA_BLACK)

/* ── Console API ────────────────────────────── */

/* Initialize the console (clear screen, reset cursor) */
void con_init(void);

/* Clear the entire screen */
void con_clear(void);

/* Print a single character at the current cursor position */
void con_putchar(char c);

/* Print a null-terminated string */
void con_print(const char *str);

/* Print a null-terminated string followed by a newline */
void con_println(const char *str);

/* Print an unsigned 16-bit integer in decimal */
void con_print_dec(uint16_t value);

/* Print an unsigned 16-bit integer in hexadecimal (with 0x prefix) */
void con_print_hex(uint16_t value);

/* Scroll the screen up by one line */
void con_scroll(void);

/* Move the hardware cursor to (row, col) */
void con_set_cursor(uint8_t row, uint8_t col);

/* Set the current color attribute */
void con_set_color(uint8_t fg, uint8_t bg);

/* Get the current cursor row */
uint8_t con_get_row(void);

/* Get the current cursor column */
uint8_t con_get_col(void);

#endif /* PHOENIX_CONSOLE_H */
