/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Text UI toolkit (see ui.h)
 *
 * The look follows modern terminal tools: rounded panels with a title
 * (Lip Gloss, Ratatui), thick-line meters and sparklines (btop), and a
 * powerline prompt and status bar (Starship, tmux), in the Catppuccin
 * Mocha palette. Everything is drawn with the console's colour roles
 * and glyphs, so it degrades to plain code-page-437 on a CGA.
 */

#include "ui.h"
#include "console.h"
#include "interrupts.h"
#include "thread.h"
#include "memory.h"
#include "hal.h"

/* ── Small helpers ──────────────────────────── */

static void put(uint8_t fg, uint8_t bg, char c)
{
    con_set_color(fg, bg);
    con_putchar(c);
}

static void repeat_to(uint8_t col, char c)
{
    while (con_get_col() < col) {
        con_putchar(c);
    }
}

/* Digits of `value` into `out` (no terminator); returns how many */
static uint8_t digits(uint32_t value, char *out)
{
    char tmp[10];
    uint8_t n = 0, i;

    do {
        tmp[n++] = (char)('0' + (uint8_t)(value % 10));
        value /= 10;
    } while (value > 0);
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

static uint8_t length(const char *s)
{
    uint8_t n = 0;
    while (s[n]) n++;
    return n;
}

/* True when the custom glyphs are loaded (the bar is not a plain block) */
static bool fancy(void)
{
    return con_glyph[G_BAR] != (uint8_t)CH_FULL;
}

/* ── Panels ─────────────────────────────────── */

void ui_panel_open(const char *title, uint8_t accent, const char *note)
{
    uint8_t note_col = UI_RIGHT;

    con_reset_color();
    con_putchar(' ');
    put(TH_OVERLAY, TH_BASE, GLYPH(G_ROUND_TL));
    con_putchar(CH_H);
    con_putchar(' ');
    con_set_color(accent, TH_BASE);
    con_print(title);
    put(TH_OVERLAY, TH_BASE, ' ');

    if (note) {
        note_col = (uint8_t)(UI_RIGHT - 3 - length(note));
    }
    repeat_to(note_col, CH_H);
    if (note) {
        con_putchar(' ');
        con_set_color(TH_SUBTEXT, TH_BASE);
        con_print(note);
        put(TH_OVERLAY, TH_BASE, ' ');
        con_putchar(CH_H);
    }
    con_putchar(GLYPH(G_ROUND_TR));
    con_reset_color();
    con_putchar('\n');
}

void ui_row(void)
{
    con_reset_color();
    con_putchar(' ');
    put(TH_OVERLAY, TH_BASE, CH_V);
    con_putchar(' ');
    con_reset_color();
}

void ui_row_end(void)
{
    con_reset_color();
    repeat_to(UI_RIGHT, ' ');
    put(TH_OVERLAY, TH_BASE, CH_V);
    con_reset_color();
    con_putchar('\n');
}

void ui_row_blank(void)
{
    ui_row();
    ui_row_end();
}

void ui_row_rule(void)
{
    ui_row();
    con_set_color(TH_SURFACE2, TH_BASE);
    repeat_to(UI_RIGHT - 1, CH_H);
    ui_row_end();
}

void ui_panel_close(void)
{
    con_reset_color();
    con_putchar(' ');
    put(TH_OVERLAY, TH_BASE, GLYPH(G_ROUND_BL));
    repeat_to(UI_RIGHT, CH_H);
    con_putchar(GLYPH(G_ROUND_BR));
    con_reset_color();
    con_putchar('\n');
}

/* ── Inside a row ───────────────────────────── */

void ui_text(uint8_t fg, const char *text)
{
    con_set_color(fg, TH_BASE);
    con_print(text);
    con_reset_color();
}

void ui_pad_to(uint8_t col)
{
    con_reset_color();
    repeat_to(col, ' ');
}

void ui_hex(uint16_t value, uint8_t fg)
{
    con_set_color(fg, TH_BASE);
    con_print_hex(value);
    con_reset_color();
}

void ui_reg(const char *name, uint16_t value)
{
    uint8_t start = con_get_col();

    ui_text(TH_MAUVE, name);
    ui_text(TH_OVERLAY, "=");
    ui_hex(value, TH_TEXT);
    ui_pad_to((uint8_t)(start + 16));
}

char *ui_format(uint32_t value, const char *suffix, char *buf, uint8_t size)
{
    char number[10];
    uint8_t count = digits(value, number);
    uint8_t n = 0, i;

    for (i = 0; i < count && n + 1 < size; i++) {
        buf[n++] = number[i];
    }
    while (*suffix && n + 1 < size) {
        buf[n++] = *suffix++;
    }
    buf[n] = '\0';
    return buf;
}

void ui_num(uint32_t value, uint8_t width, uint8_t fg)
{
    char buf[10];
    uint8_t n = digits(value, buf), i;

    con_set_color(fg, TH_BASE);
    for (i = n; i < width; i++) {
        con_putchar(' ');
    }
    for (i = 0; i < n; i++) {
        con_putchar(buf[i]);
    }
    con_reset_color();
}

void ui_meter(uint32_t used, uint32_t total, uint8_t width, uint8_t fg)
{
    uint8_t filled = total ? (uint8_t)((used * width + total / 2) / total) : 0;
    uint8_t i;

    if (filled > width) filled = width;
    if (used > 0 && filled == 0) filled = 1;      /* never hide that something is used */

    con_set_color(fg, TH_BASE);
    for (i = 0; i < filled; i++) {
        con_putchar(GLYPH(G_BAR));
    }
    con_set_color(TH_SURFACE2, TH_BASE);
    for (; i < width; i++) {
        con_putchar(fancy() ? CH_H : CH_SHADE);
    }
    con_reset_color();
}

void ui_counter(const char *label, uint32_t value, const char *unit)
{
    uint8_t start = con_get_col();

    ui_text(TH_SUBTEXT, label);
    ui_pad_to((uint8_t)(start + 20));
    ui_num(value, 10, TH_TEXT);
    if (unit) {
        con_putchar(' ');
        ui_text(TH_OVERLAY, unit);
    }
    ui_pad_to((uint8_t)(start + 37));
}

void ui_kv(const char *label, uint8_t width, uint8_t fg, const char *value)
{
    uint8_t start = con_get_col();

    ui_text(TH_SUBTEXT, label);
    ui_pad_to((uint8_t)(start + width));
    ui_text(fg, value);
}

void ui_pill(const char *text, uint8_t bg)
{
    put(bg, TH_BASE, GLYPH(G_PILL_L));
    con_set_color(TH_CRUST, bg);
    con_print(text);
    put(bg, TH_BASE, GLYPH(G_PILL_R));
    con_reset_color();
}

void ui_dot(uint8_t fg, const char *text)
{
    put(fg, TH_BASE, GLYPH(G_BULLET));
    con_putchar(' ');
    con_set_color(fg, TH_BASE);
    con_print(text);
    con_reset_color();
}

/* ── Lines outside panels ───────────────────── */

static void marked(char mark, uint8_t fg, const char *text)
{
    con_reset_color();
    con_print("  ");
    put(fg, TH_BASE, mark);
    con_putchar(' ');
    con_reset_color();
    con_println(text);
}

void ui_ok(const char *text)
{
    marked(GLYPH(G_CHECK), TH_GREEN, text);
}

void ui_fail(const char *text)
{
    marked(GLYPH(G_CROSS), TH_RED, text);
}

void ui_note(uint8_t accent, const char *text)
{
    marked(GLYPH(G_BULLET), accent, text);
}

#define STEP_RESULT_COL 32

void ui_step(const char *label)
{
    con_reset_color();
    con_print("  ");
    put(TH_MAUVE, TH_BASE, GLYPH(G_BULLET));
    con_putchar(' ');
    con_reset_color();
    con_print(label);
    con_putchar(' ');
    con_set_color(TH_SURFACE2, TH_BASE);
    repeat_to(STEP_RESULT_COL - 1, CH_DOT);
    con_reset_color();
    con_putchar(' ');
}

void ui_mark(bool ok)
{
    put(ok ? TH_GREEN : TH_RED, TH_BASE, GLYPH(ok ? G_CHECK : G_CROSS));
    con_reset_color();
    con_putchar(' ');
}

/* ── Logo ───────────────────────────────────── */

/* "PHOENIX" in half blocks, three rows per letter; colours run like a flame */
static const char *const logo[7][3] = {
    { "\xDB\xDF\xDF\xDF\xDC", "\xDB\xDC\xDC\xDC\xDF", "\xDB    " },      /* P */
    { "\xDB   \xDB", "\xDB\xDF\xDF\xDF\xDB", "\xDB   \xDB" },              /* H */
    { "\xDC\xDF\xDF\xDF\xDC", "\xDB   \xDB", "\xDF\xDC\xDC\xDC\xDF" },   /* O */
    { "\xDB\xDF\xDF\xDF\xDF", "\xDB\xDF\xDF\xDF ", "\xDB\xDC\xDC\xDC\xDC" },  /* E */
    { "\xDB\xDC  \xDB", "\xDB \xDF\xDC\xDB", "\xDB   \xDB" },                 /* N */
    { "\xDF\xDB\xDF", " \xDB ", "\xDC\xDB\xDC" },                            /* I */
    { "\xDF\xDC \xDC\xDF", "  \xDB  ", "\xDC\xDF \xDF\xDC" },               /* X */
};
static const uint8_t logo_colour[7] = {
    TH_MAUVE, TH_PINK, TH_PINK, TH_RED, TH_RED, TH_PEACH, TH_YELLOW
};

void ui_logo(uint8_t col)
{
    uint8_t row, letter;

    for (row = 0; row < 3; row++) {
        ui_pad_to(col);
        for (letter = 0; letter < 7; letter++) {
            con_set_color(logo_colour[letter], TH_BASE);
            con_print(logo[letter][row]);
            con_putchar(' ');
        }
        con_reset_color();
        con_putchar('\n');
    }
}

/* ── Prompt ─────────────────────────────────── */

void ui_prompt(void)
{
    con_mirror(false);
    put(TH_MAUVE, TH_BASE, GLYPH(G_PILL_L));
    con_set_color(TH_CRUST, TH_MAUVE);
    con_print(" phoenix ");
    put(TH_MAUVE, TH_BASE, GLYPH(G_ARROW_R));
    con_reset_color();
    con_putchar(' ');
    con_mirror(true);
    con_mirror_text("phoenix> ");
}

/* ── Status bar ─────────────────────────────── */

#define LOAD_SAMPLES    8

static bool     status_on;
static uint8_t  countdown;
static uint8_t  status_col;
static uint32_t last_idle_ticks;
static uint8_t  load[LOAD_SAMPLES];     /* Busy percentage, one per second, oldest first */
static bool     have_sample;

static void sb_put(char c, uint8_t fg, uint8_t bg)
{
    con_put_at(STATUS_ROW, status_col++, c, fg, bg);
}

static void sb_text(const char *s, uint8_t fg, uint8_t bg)
{
    while (*s) {
        sb_put(*s++, fg, bg);
    }
}

static void sb_num(uint32_t value, uint8_t min_digits, uint8_t fg)
{
    char buf[10];
    uint8_t n = digits(value, buf), i;

    for (i = n; i < min_digits; i++) {
        sb_put('0', fg, TH_SURFACE);
    }
    for (i = 0; i < n; i++) {
        sb_put(buf[i], fg, TH_SURFACE);
    }
}

static void sb_label(const char *label)
{
    sb_put(' ', TH_TEXT, TH_SURFACE);
    sb_text(label, TH_SUBTEXT, TH_SURFACE);
    sb_put(' ', TH_TEXT, TH_SURFACE);
}

static void sb_separator(void)
{
    sb_put(' ', TH_TEXT, TH_SURFACE);
    sb_put(CH_V, TH_OVERLAY, TH_SURFACE);
}

static uint8_t load_colour(uint8_t busy)
{
    return busy < 50 ? TH_GREEN : busy < 80 ? TH_YELLOW : TH_RED;
}

static void sample_load(void)
{
    uint32_t idle = thread_get_tcb(0)->cpu_ticks;
    uint32_t idle_delta = idle - last_idle_ticks;
    uint8_t i;

    last_idle_ticks = idle;
    if (!have_sample) {
        have_sample = true;
        return;
    }
    if (idle_delta > HZ) idle_delta = HZ;
    for (i = 0; i + 1 < LOAD_SAMPLES; i++) {
        load[i] = load[i + 1];
    }
    load[LOAD_SAMPLES - 1] = (uint8_t)(100 - (uint16_t)idle_delta * 100 / HZ);
}

static void status_draw(void)
{
    static const char *const cpu_names[] = { " 8086 ", " 186 ", " 286+ " };
    const char *cpu = cpu_names[hal_cpu_class()];
    uint32_t seconds = tick_count / HZ;
    uint8_t busy = load[LOAD_SAMPLES - 1];
    uint8_t i;

    status_col = 0;

    /* Brand pill, ending in a powerline arrow */
    sb_put(GLYPH(G_PILL_L), TH_MAUVE, TH_SURFACE);
    sb_text(" PHOENIX ", TH_CRUST, TH_MAUVE);
    sb_put(GLYPH(G_ARROW_R), TH_MAUVE, TH_SURFACE);

    sb_label("up");
    sb_num(seconds / 3600, 2, TH_TEXT);
    sb_put(':', TH_OVERLAY, TH_SURFACE);
    sb_num((seconds / 60) % 60, 2, TH_TEXT);
    sb_put(':', TH_OVERLAY, TH_SURFACE);
    sb_num(seconds % 60, 2, TH_TEXT);

    sb_separator();
    sb_label("threads");
    sb_num((uint32_t)thread_count(), 1, TH_TEXT);

    sb_separator();
    sb_label("free");
    sb_num((uint32_t)far_free_paras() / 64, 1, TH_TEXT);
    sb_put('K', TH_SUBTEXT, TH_SURFACE);

    /* CPU load over the last eight seconds, as a sparkline */
    sb_separator();
    sb_label("load");
    for (i = 0; i < LOAD_SAMPLES; i++) {
        uint8_t level = (uint8_t)((load[i] + 13) / 14);
        if (level > 7) level = 7;
        if (level == 0) {
            sb_put(GLYPH(G_SPARK1), TH_OVERLAY, TH_SURFACE);
        } else {
            sb_put(GLYPH(G_SPARK1 + level - 1), load_colour(load[i]), TH_SURFACE);
        }
    }
    sb_put(' ', TH_TEXT, TH_SURFACE);
    if (busy < 100) sb_put(' ', TH_TEXT, TH_SURFACE);
    if (busy < 10) sb_put(' ', TH_TEXT, TH_SURFACE);
    sb_num(busy, 1, load_colour(busy));
    sb_put('%', TH_SUBTEXT, TH_SURFACE);

    /* Right-hand segment: the processor the kernel detected */
    while (status_col < VGA_WIDTH - 1 - length(cpu)) {
        sb_put(' ', TH_TEXT, TH_SURFACE);
    }
    sb_put(GLYPH(G_ARROW_L), TH_BLUE, TH_SURFACE);
    sb_text(cpu, TH_CRUST, TH_BLUE);
}

void ui_status_start(void)
{
    uint16_t flags = hal_irq_save();

    last_idle_ticks = thread_get_tcb(0)->cpu_ticks;
    have_sample = true;
    countdown = HZ;
    status_on = true;
    status_draw();
    hal_irq_restore(flags);
}

void ui_status_stop(void)
{
    status_on = false;
}

void ui_status_halted(void)
{
    status_on = false;
    status_col = 0;
    sb_put(' ', TH_CRUST, TH_RED);
    sb_put(GLYPH(G_CROSS), TH_CRUST, TH_RED);
    sb_text(" HALTED ", TH_CRUST, TH_RED);
    sb_put(GLYPH(G_ARROW_R), TH_RED, TH_SURFACE);
    sb_text(" the kernel stopped after a panic; press reset to restart", TH_SUBTEXT, TH_SURFACE);
    while (status_col < VGA_WIDTH) {
        sb_put(' ', TH_TEXT, TH_SURFACE);
    }
}

void ui_timer_tick(void)
{
    if (status_on && --countdown == 0) {
        countdown = HZ;
        sample_load();
        status_draw();
    }
}
