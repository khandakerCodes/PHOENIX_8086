/*
 * Phoenix-8086 — Console Driver Implementation
 *
 * Direct VGA text-mode framebuffer writes at B800:0000.
 * Supports character output, scrolling, cursor positioning,
 * and color control without relying on BIOS services.
 */

#include "console.h"
#include "hal.h"

/* ── Internal state ─────────────────────────── */
static uint8_t  cursor_row = 0;
static uint8_t  cursor_col = 0;
static uint8_t  current_color = VGA_DEFAULT_COLOR;

/*
 * Pointer to VGA text memory.
 * The kernel data segment is not segment B800h, so the
 * framebuffer is reached through a far pointer.
 */
#define VGA_PTR ((uint16_t __far *)MK_FP(0xB800, 0))

static void serial_putchar(char c)
{
    /* Mirror output to COM1 (0x3F8) for serial console */
    uint16_t timeout = 1000;
    while ((inb(0x3F8 + 5) & 0x20) == 0 && --timeout);
    outb(0x3F8, c);
}

/* ── Internal: update hardware cursor ───────── */
static void update_hw_cursor(void)
{
    uint16_t pos = (uint16_t)cursor_row * VGA_WIDTH + cursor_col;

    /* CRT controller: cursor location high byte */
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));

    /* CRT controller: cursor location low byte */
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

/* ── Internal: write char + attr to VGA memory ─ */
static void vga_write(uint8_t row, uint8_t col, char c, uint8_t color)
{
    uint16_t index = (uint16_t)row * VGA_WIDTH + col;

    VGA_PTR[index] = ((uint16_t)color << 8) | (uint8_t)c;
}

/* ── Internal: read char from VGA memory ───── */
static uint16_t vga_read(uint8_t row, uint8_t col)
{
    uint16_t index = (uint16_t)row * VGA_WIDTH + col;

    return VGA_PTR[index];
}

/* ── Public API ─────────────────────────────── */

void con_init(void)
{
    cursor_row = 0;
    cursor_col = 0;
    current_color = VGA_DEFAULT_COLOR;
    con_clear();
}

void con_clear(void)
{
    uint8_t row, col;
    for (row = 0; row < VGA_HEIGHT; row++) {
        for (col = 0; col < VGA_WIDTH; col++) {
            vga_write(row, col, ' ', current_color);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    update_hw_cursor();
}

void con_scroll(void)
{
    uint8_t row, col;
    uint16_t cell;

    /* Move all lines up by one */
    for (row = 1; row < VGA_HEIGHT; row++) {
        for (col = 0; col < VGA_WIDTH; col++) {
            cell = vga_read(row, col);
            vga_write(row - 1, col, (char)(cell & 0xFF), (uint8_t)(cell >> 8));
        }
    }

    /* Clear the last line */
    for (col = 0; col < VGA_WIDTH; col++) {
        vga_write(VGA_HEIGHT - 1, col, ' ', current_color);
    }
}

void con_putchar(char c)
{
    /* Threads share the cursor; keep each character update atomic */
    uint16_t flags = hal_irq_save();

    serial_putchar(c);

    if (c == '\n') {
        /* Newline: move to start of next line */
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        /* Carriage return: move to start of current line */
        cursor_col = 0;
    } else if (c == '\b') {
        /* Backspace: move cursor back and erase */
        if (cursor_col > 0) {
            cursor_col--;
            vga_write(cursor_row, cursor_col, ' ', current_color);
        }
    } else if (c == '\t') {
        /* Tab: advance to next 8-column boundary */
        uint8_t next = (cursor_col + 8) & ~7;
        if (next >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        } else {
            cursor_col = next;
        }
    } else {
        /* Normal character */
        vga_write(cursor_row, cursor_col, c, current_color);
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    /* Scroll if we've gone past the bottom */
    if (cursor_row >= VGA_HEIGHT) {
        con_scroll();
        cursor_row = VGA_HEIGHT - 1;
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

    /* Print in reverse order */
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
    current_color = VGA_COLOR(fg, bg);
}

uint8_t con_get_row(void)
{
    return cursor_row;
}

uint8_t con_get_col(void)
{
    return cursor_col;
}
