/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Console Driver Header
 *
 * Text-mode console written straight to the framebuffer at B800:0000.
 *
 * The screen has two parts: rows 0-23 scroll and take everything the
 * kernel prints; row 24 is the status bar (kernel/ui.c), which never
 * scrolls and is never sent to telemetry.
 *
 * On a VGA the console loads a colour theme (Catppuccin Mocha) into the
 * palette, turns blinking off so all 16 colours work as backgrounds,
 * and adds a few glyphs to the font: rounded corners, powerline arrows,
 * pill caps, sparkline bars, check and cross marks. On CGA-class
 * hardware the same code falls back to the nearest standard colours and
 * code-page-437 characters, so nothing breaks, it is only plainer.
 */

#ifndef PHOENIX_CONSOLE_H
#define PHOENIX_CONSOLE_H

#include "../include/types.h"

/* Text mode geometry */
#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_MEMORY      0xB8000
#define CON_ROWS        24      /* Scrolling rows; the last screen row is the status bar */
#define STATUS_ROW      24

/*
 * Colour roles. Code asks for a role, never a raw hardware colour; the
 * console turns it into a palette slot (VGA) or the nearest standard
 * colour (CGA). The names and values follow Catppuccin Mocha.
 */
#define TH_BASE         0       /* #1e1e2e  screen background */
#define TH_SURFACE      1       /* #313244  panels, status bar */
#define TH_SURFACE2     2       /* #45475a  selection, empty meter */
#define TH_OVERLAY      3       /* #6c7086  borders, muted text */
#define TH_SUBTEXT      4       /* #a6adc8  secondary text */
#define TH_TEXT         5       /* #cdd6f4  body text */
#define TH_CRUST        6       /* #11111b  text on coloured pills */
#define TH_MAUVE        7       /* #cba6f7  brand accent */
#define TH_BLUE         8       /* #89b4fa */
#define TH_SKY          9       /* #89dceb */
#define TH_TEAL         10      /* #94e2d5 */
#define TH_GREEN        11      /* #a6e3a1  success */
#define TH_YELLOW       12      /* #f9e2af  warning */
#define TH_PEACH        13      /* #fab387 */
#define TH_RED          14      /* #f38ba8  error */
#define TH_PINK         15      /* #f5c2e7 */

/*
 * UI glyphs. Print GLYPH(G_x): the console picks the character that
 * draws it on this display (a custom glyph on VGA, the closest
 * code-page-437 character otherwise).
 */
enum {
    G_ROUND_TL, G_ROUND_TR, G_ROUND_BL, G_ROUND_BR,   /* ╭ ╮ ╰ ╯ */
    G_PILL_L, G_PILL_R,                               /* rounded ends of a pill */
    G_ARROW_R, G_ARROW_L,                             /* solid powerline arrows */
    G_BAR,                                            /* thick meter segment ━ */
    G_CHECK, G_CROSS, G_BULLET, G_CHEVRON,            /* ✓ ✗ ● ❯ */
    G_SPARK1, G_SPARK2, G_SPARK3, G_SPARK4,           /* ▁ ▂ ▃ ▄ */
    G_SPARK5, G_SPARK6, G_SPARK7,                     /* ▅ ▆ ▇ */
    G_COUNT
};
extern uint8_t con_glyph[G_COUNT];
#define GLYPH(g)        ((char)con_glyph[g])

/* Code-page-437 characters used by the UI */
#define CH_H            '\xC4'  /* ─ */
#define CH_V            '\xB3'  /* │ */
#define CH_DOT          '\xFA'  /* · */
#define CH_FULL         '\xDB'  /* █ */
#define CH_SHADE        '\xB0'  /* ░ */

/* ── Console API ────────────────────────────── */

/* Set up the display: theme, glyphs, empty screen (cursor at the top) */
void con_init(void);

/* True if the display is a VGA, with the theme and custom glyphs loaded */
bool con_has_vga(void);

/* Clear the scrolling rows; the status bar is left alone */
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

/* Scroll the scrolling rows up by one line */
void con_scroll(void);

/* Move the cursor to (row, col) */
void con_set_cursor(uint8_t row, uint8_t col);

/* Set the colour for what is printed next: two colour roles (TH_*) */
void con_set_color(uint8_t fg, uint8_t bg);

/* Back to body text on the background */
void con_reset_color(void);

/* Get the current cursor row and column */
uint8_t con_get_row(void);
uint8_t con_get_col(void);

/*
 * Turn mirroring of printed text to telemetry (or to the serial port)
 * off and on. Screen decoration is drawn with mirroring off, so the
 * dashboard and the tests see the same plain text as before.
 */
void con_mirror(bool on);

/* Send text to telemetry only, as if printed, without drawing it */
void con_mirror_text(const char *text);

/* Write one cell anywhere on the screen, cursor untouched (status bar, panic screen) */
void con_put_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);

#endif /* PHOENIX_CONSOLE_H */
