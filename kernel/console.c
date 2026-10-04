/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Console Driver Implementation
 *
 * Direct text-mode framebuffer writes at B800:0000: characters,
 * scrolling, the cursor and colours, without BIOS services.
 *
 * At start-up the console asks the BIOS once whether the display is a
 * VGA. If it is, it loads the colour theme into the palette, turns
 * blinking off (16 background colours) and adds the UI glyphs to the
 * font; see console.h. Otherwise roles map to the 16 standard colours
 * and glyphs to their closest code-page-437 characters.
 */

#include "console.h"
#include "hal.h"
#include "serial.h"
#include "telemetry.h"

/* make CONSOLE=plain: behave as on a CGA even on a VGA, to see the fallback look */
#ifndef CONFIG_CONSOLE_PLAIN
#define CONFIG_CONSOLE_PLAIN 0
#endif

/* ── Internal state ─────────────────────────── */
static uint8_t  cursor_row = 0;
static uint8_t  cursor_col = 0;     /* VGA_WIDTH means "wrap before the next character" */
static uint8_t  current_attr;
static bool     mirroring = true;
static bool     vga;                /* Theme and glyphs loaded */
static bool     custom_font;        /* The glyphs in con_glyph are our own */

/* Colour role → attribute colour (identity on a VGA, whose palette holds the theme) */
static uint8_t  role_colour[16];

uint8_t con_glyph[G_COUNT];

/*
 * Pointer to text memory. The kernel data segment is not segment
 * B800h, so the framebuffer is reached through a far pointer.
 */
#define VGA_PTR ((uint16_t __far *)MK_FP(0xB800, 0))

#define ATTR(fg, bg)    ((uint8_t)((role_colour[(bg) & 15] << 4) | role_colour[(fg) & 15]))

/* ── Theme ──────────────────────────────────── */

/* Catppuccin Mocha, in role order (console.h), as 8-bit RGB */
static const uint8_t theme_rgb[16][3] = {
    { 0x1E, 0x1E, 0x2E }, { 0x31, 0x32, 0x44 }, { 0x45, 0x47, 0x5A }, { 0x6C, 0x70, 0x86 },
    { 0xA6, 0xAD, 0xC8 }, { 0xCD, 0xD6, 0xF4 }, { 0x11, 0x11, 0x1B }, { 0xCB, 0xA6, 0xF7 },
    { 0x89, 0xB4, 0xFA }, { 0x89, 0xDC, 0xEB }, { 0x94, 0xE2, 0xD5 }, { 0xA6, 0xE3, 0xA1 },
    { 0xF9, 0xE2, 0xAF }, { 0xFA, 0xB3, 0x87 }, { 0xF3, 0x8B, 0xA8 }, { 0xF5, 0xC2, 0xE7 },
};

/* Without a VGA: the nearest of the 16 standard CGA colours, per role */
static const uint8_t cga_colour[16] = {
    0x0, 0x1, 0x8, 0x8, 0x7, 0x7, 0x0, 0xD,
    0x9, 0xB, 0x3, 0xA, 0xE, 0x6, 0xC, 0xD,
};

/* The DAC register each of the 16 text colours uses in the BIOS's text mode */
static const uint8_t dac_index[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

/* INT 10h AX=1A00h: display combination code; BL 7 or 8 is a VGA */
static bool detect_vga(void)
{
    uint16_t ax = 0x1A00;
    uint16_t bx = 0;

    __asm__ __volatile__("int $0x10" : "+a"(ax), "+b"(bx) : : "cx", "dx", "si", "di", "memory");
    return (ax & 0xFF) == 0x1A && ((bx & 0xFF) == 7 || (bx & 0xFF) == 8);
}

static void load_palette(void)
{
    uint8_t i;

    for (i = 0; i < 16; i++) {
        outb(0x3C8, dac_index[i]);
        outb(0x3C9, theme_rgb[i][0] >> 2);  /* the DAC takes 6 bits per channel */
        outb(0x3C9, theme_rgb[i][1] >> 2);
        outb(0x3C9, theme_rgb[i][2] >> 2);
    }
}

/* Attribute bit 7 selects a bright background instead of blinking */
static void blink_off(void)
{
    uint8_t mode;

    if (vga) {
        (void)inb(0x3DA);           /* attribute controller: back to the index state */
        outb(0x3C0, 0x30);          /* mode control register, display left on */
        mode = inb(0x3C1);
        outb(0x3C0, mode & (uint8_t)~0x08);
    } else {
        outb(0x3D8, 0x09);          /* CGA mode control: 80x25 text, video on, no blink */
    }
}

/* ── Glyphs ─────────────────────────────────── */

/*
 * Where each glyph goes in the font, and what stands in for it without
 * one. Glyphs that must join the cell to their right (rounded corners
 * opening right, pill caps, bars) use codes C0h-DFh: on a VGA those
 * repeat their last pixel column into the ninth, so they meet the next
 * cell without a gap. The codes replaced are double-line box pieces the
 * kernel never prints.
 */
static const uint8_t glyph_code[G_COUNT] = {
    0xC9, 0xBB, 0xC8, 0xBC,             /* ╔ ╗ ╚ ╝ */
    0xD5, 0xB5,
    0xB6, 0xD6,
    0xCE,
    0xB7, 0xB8, 0xBD, 0xBE,
    0xC6, 0xC7, 0xCA, 0xCB, 0xCC, 0xCF, 0xD0,
};

static const uint8_t glyph_fallback[G_COUNT] = {
    0xDA, 0xBF, 0xC0, 0xD9,             /* ┌ ┐ └ ┘ */
    0xDE, 0xDD,                         /* ▐ ▌ */
    0x10, 0x11,                         /* ► ◄ */
    0xDB,                               /* █ */
    0xFB, 'x', 0xFE, 0xAF,              /* √ x ■ » */
    '_', '_', 0xDC, 0xDC, 0xDC, 0xDB, 0xDB,
};

/* 8 x 16 bitmaps, generated from pictures (see the comments) */
static const uint8_t glyph_bits[G_COUNT][16] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 },  /* ╭ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x30, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 },  /* ╮ */
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0C, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* ╰ */
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* ╯ */
    { 0x07, 0x1F, 0x3F, 0x7F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x3F, 0x1F, 0x07 },  /* pill ( */
    { 0xE0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFC, 0xF8, 0xE0 },  /* pill ) */
    { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80 },  /* arrow > */
    { 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF, 0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01 },  /* arrow < */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* ━ */
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x06, 0x8C, 0xD8, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* ✓ */
    { 0x00, 0x00, 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* ✗ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* ● */
    { 0x00, 0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* ❯ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF },  /* ▁ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF },  /* ▂ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },  /* ▃ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },  /* ▄ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },  /* ▅ */
    { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },  /* ▆ */
    { 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },  /* ▇ */
};

static void seq(uint8_t index, uint8_t value)
{
    outb(0x3C4, index);
    outb(0x3C5, value);
}

static void gfx(uint8_t index, uint8_t value)
{
    outb(0x3CE, index);
    outb(0x3CF, value);
}

/*
 * The font lives in plane 2 of video memory. For the copy, map that
 * plane alone at A000:0000 (32 bytes per character), then restore the
 * text-mode setup. Only done for the standard 16-line character cell.
 */
static bool load_glyphs(void)
{
    uint8_t height, g, i;
    uint16_t flags;

    outb(0x3D4, 0x09);                  /* CRTC: maximum scan line */
    height = (uint8_t)((inb(0x3D5) & 0x1F) + 1);
    if (height != 16) {
        return false;
    }

    flags = hal_irq_save();
    seq(0x00, 0x01);                    /* synchronous reset */
    seq(0x02, 0x04);                    /* write plane 2 only */
    seq(0x04, 0x07);                    /* sequential addressing */
    seq(0x00, 0x03);
    gfx(0x04, 0x02);                    /* read plane 2 */
    gfx(0x05, 0x00);                    /* no odd/even */
    gfx(0x06, 0x04);                    /* map 64 KB at A000h, graphics addressing */

    for (g = 0; g < G_COUNT; g++) {
        uint8_t __far *cell = (uint8_t __far *)MK_FP(0xA000, (uint16_t)glyph_code[g] * 32);
        for (i = 0; i < 16; i++) {
            cell[i] = glyph_bits[g][i];
        }
    }

    seq(0x00, 0x01);
    seq(0x02, 0x03);                    /* planes 0 and 1: characters and attributes */
    seq(0x04, 0x03);                    /* odd/even addressing */
    seq(0x00, 0x03);
    gfx(0x04, 0x00);
    gfx(0x05, 0x10);
    gfx(0x06, 0x0E);                    /* map 32 KB at B800h, text */
    hal_irq_restore(flags);
    return true;
}

/* ── Mirror ─────────────────────────────────── */

/* Telemetry gets standard code-page-437 text, never our redefined codes */
static char mirror_char(char c)
{
    uint8_t g;

    if (custom_font) {
        for (g = 0; g < G_COUNT; g++) {
            if ((uint8_t)c == glyph_code[g]) {
                return (char)glyph_fallback[g];
            }
        }
    }
    return c;
}

/*
 * Mirror console output off the machine: as CONSOLE telemetry records
 * normally, or as plain serial text when telemetry is compiled out.
 */
static void mirror_putchar(char c)
{
    c = mirror_char(c);
#if CONFIG_TELEMETRY
    telemetry_console_char(c);
#else
    serial_putc((uint8_t)c);
#endif
}

void con_mirror(bool on)
{
    mirroring = on;
}

void con_mirror_text(const char *text)
{
    uint16_t flags = hal_irq_save();

    while (*text) {
        mirror_putchar(*text++);
    }
    hal_irq_restore(flags);
}

/* ── Cells and cursor ───────────────────────── */

static void update_hw_cursor(void)
{
    uint8_t col = cursor_col < VGA_WIDTH ? cursor_col : VGA_WIDTH - 1;
    uint16_t pos = (uint16_t)cursor_row * VGA_WIDTH + col;

    outb(0x3D4, 0x0E);                  /* CRTC: cursor location high byte */
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 0x0F);                  /* low byte */
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void vga_write(uint8_t row, uint8_t col, char c, uint8_t attr)
{
    VGA_PTR[(uint16_t)row * VGA_WIDTH + col] = ((uint16_t)attr << 8) | (uint8_t)c;
}

void con_put_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg)
{
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        vga_write(row, col, c, ATTR(fg, bg));
    }
}

/* ── Public API ─────────────────────────────── */

void con_init(void)
{
    uint8_t i;

    vga = !CONFIG_CONSOLE_PLAIN && detect_vga();
    for (i = 0; i < 16; i++) {
        role_colour[i] = vga ? i : cga_colour[i];
    }
    if (vga) {
        load_palette();
        custom_font = load_glyphs();
    }
    for (i = 0; i < G_COUNT; i++) {
        con_glyph[i] = custom_font ? glyph_code[i] : glyph_fallback[i];
    }
    blink_off();

    con_reset_color();
    for (i = 0; i < VGA_WIDTH; i++) {
        vga_write(STATUS_ROW, i, ' ', ATTR(TH_TEXT, TH_SURFACE));
    }
    con_clear();
}

bool con_has_vga(void)
{
    return vga;
}

void con_clear(void)
{
    uint8_t row, col;

    for (row = 0; row < CON_ROWS; row++) {
        for (col = 0; col < VGA_WIDTH; col++) {
            vga_write(row, col, ' ', current_attr);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    update_hw_cursor();
}

void con_scroll(void)
{
    uint16_t __far *screen = VGA_PTR;
    uint16_t i;
    uint16_t blank = ((uint16_t)current_attr << 8) | ' ';

    for (i = 0; i < (CON_ROWS - 1) * VGA_WIDTH; i++) {
        screen[i] = screen[i + VGA_WIDTH];
    }
    for (; i < CON_ROWS * VGA_WIDTH; i++) {
        screen[i] = blank;
    }
}

static void new_line(void)
{
    cursor_col = 0;
    if (++cursor_row >= CON_ROWS) {
        con_scroll();
        cursor_row = CON_ROWS - 1;
    }
}

void con_putchar(char c)
{
    /* Threads share the cursor; keep each character update atomic */
    uint16_t flags = hal_irq_save();

    if (mirroring) {
        mirror_putchar(c);
    }

    if (c == '\n') {
        new_line();
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            if (cursor_col > VGA_WIDTH - 1) cursor_col = VGA_WIDTH;
            cursor_col--;
            vga_write(cursor_row, cursor_col, ' ', current_attr);
        }
    } else if (c == '\t') {
        cursor_col = (uint8_t)((cursor_col + 8) & ~7);
        if (cursor_col >= VGA_WIDTH) {
            new_line();
        }
    } else {
        /*
         * A line that ends exactly at the right edge wraps only when the
         * next character arrives, as on a terminal, so a full-width line
         * followed by a newline does not leave an empty line.
         */
        if (cursor_col >= VGA_WIDTH) {
            new_line();
        }
        vga_write(cursor_row, cursor_col, c, current_attr);
        cursor_col++;
    }

    update_hw_cursor();
    hal_irq_restore(flags);
}

void con_print(const char *str)
{
    while (*str) {
        con_putchar(*str);
        str++;
    }
}

void con_println(const char *str)
{
    con_print(str);
    con_putchar('\n');
}

void con_print_dec(uint16_t value)
{
    char buf[6];  /* Max 5 digits + null */
    int i = 0;

    if (value == 0) {
        con_putchar('0');
        return;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0) {
        con_putchar(buf[--i]);
    }
}

void con_print_hex(uint16_t value)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    int i;

    con_putchar('0');
    con_putchar('x');

    for (i = 12; i >= 0; i -= 4) {
        con_putchar(hex_chars[(value >> i) & 0xF]);
    }
}

void con_set_cursor(uint8_t row, uint8_t col)
{
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        cursor_row = row;
        cursor_col = col;
        update_hw_cursor();
    }
}

void con_set_color(uint8_t fg, uint8_t bg)
{
    current_attr = ATTR(fg, bg);
}

void con_reset_color(void)
{
    con_set_color(TH_TEXT, TH_BASE);
}

uint8_t con_get_row(void)
{
    return cursor_row;
}

uint8_t con_get_col(void)
{
    return cursor_col;
}
