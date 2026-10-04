/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Text UI toolkit
 *
 * The building blocks every screen is drawn with, so the shell, the
 * boot log and the panic screen share one look: panels with rounded
 * borders and a title, meters, coloured pills, check and cross marks,
 * a powerline prompt, and a status bar on the bottom row.
 *
 * A panel is 78 columns wide, starting at column 1:
 *
 *    ╭─ Title ─────────────────────────────────────── note ─╮
 *    │ content                                              │
 *    ╰──────────────────────────────────────────────────────╯
 *
 * Panel text is mirrored to telemetry like any console output, with
 * standard code-page-437 borders; the prompt and the status bar are
 * screen-only (see con_mirror).
 */

#ifndef PHOENIX_UI_H
#define PHOENIX_UI_H

#include "../include/types.h"

#define UI_LEFT         1       /* Column of a panel's left border */
#define UI_RIGHT        78      /* Column of its right border */
#define UI_TEXT_LEFT    3       /* First column of content */

/* ── Panels ─────────────────────────────────── */

/* Top border with a title in `accent`, and an optional note on the right (or NULL) */
void ui_panel_open(const char *title, uint8_t accent, const char *note);

/* Start a content row; finish it with ui_row_end */
void ui_row(void);
void ui_row_end(void);

/* A row with nothing in it */
void ui_row_blank(void);

/* A thin rule across the panel, for a table header */
void ui_row_rule(void);

/* Bottom border */
void ui_panel_close(void);

/* ── Inside a row ───────────────────────────── */

/* Text in a colour role */
void ui_text(uint8_t fg, const char *text);

/* Spaces up to an absolute screen column */
void ui_pad_to(uint8_t col);

/* A 16-bit value as 0x1234 */
void ui_hex(uint16_t value, uint8_t fg);

/* "AX=0x1234" in a 16-column cell */
void ui_reg(const char *name, uint16_t value);

/* A number and a suffix as text in `buf` (`size` bytes, truncated to fit), for a panel note */
char *ui_format(uint32_t value, const char *suffix, char *buf, uint8_t size);

/* A number right-aligned in `width` columns */
void ui_num(uint32_t value, uint8_t width, uint8_t fg);

/* A meter `width` cells long: the used part in `fg`, the rest dim */
void ui_meter(uint32_t used, uint32_t total, uint8_t width, uint8_t fg);

/* "Label:" and a right-aligned number in half a panel (37 columns), with a unit or NULL */
void ui_counter(const char *label, uint32_t value, const char *unit);

/* A label and a value: "label  value" with the label muted */
void ui_kv(const char *label, uint8_t width, uint8_t fg, const char *value);

/* A pill: rounded ends, `text` in dark letters on `bg`, on the panel background */
void ui_pill(const char *text, uint8_t bg);

/* A coloured dot and a word, for states */
void ui_dot(uint8_t fg, const char *text);

/* ── Lines outside panels ───────────────────── */

/* "✓ text" in green, "✗ text" in red, "● text" in an accent, each on its own line */
void ui_ok(const char *text);
void ui_fail(const char *text);
void ui_note(uint8_t accent, const char *text);

/* A boot-log line: "  ● label ········· " then the caller prints the result */
void ui_step(const char *label);

/* The mark that ends a step: a green check or a red cross, and a space */
void ui_mark(bool ok);

/* The PHOENIX logo, in a flame gradient, starting at `col` */
void ui_logo(uint8_t col);

/* The shell prompt; telemetry sees "phoenix> " as before */
void ui_prompt(void);

/* ── Status bar ─────────────────────────────── */

/* Draw the status bar now, and from now on once a second */
void ui_status_start(void);

/* Stop updating it (the panic screen takes the whole display) */
void ui_status_stop(void);

/* Stop updating it and turn it into a red "halted" bar */
void ui_status_halted(void);

/* Called by the timer interrupt on every tick */
void ui_timer_tick(void);

#endif /* PHOENIX_UI_H */
