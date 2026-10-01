/* ================================================================
 * NumWorks OS — ILI9341 Display Driver (NumWorks N0110)
 * File: hal/display.c
 *
 * NumWorks wires the LCD via a 16-bit parallel FSMC interface.
 * The framebuffer (153 KB) lives in the same RAM region as the
 * main stack/heap — careful layout in linker script avoids overlap.
 *
 * For a first port, we implement a software framebuffer approach:
 * write to g_framebuf, call display_flush() to DMA-push to LCD.
 *
 * Code size target: < 4 KB (driver) + 153 KB (framebuf in RAM)
 * ================================================================ */
#include "display.h"
#include "font.h"
#include "hal.h"
#include "../include/stm32f730.h"
#include "../include/string.h"

/* Framebuffer — 320*240*2 = 153,600 bytes in .bss */
uint16_t g_framebuf[LCD_WIDTH * LCD_HEIGHT] __attribute__((section(".framebuf")));

/* Set by every drawing primitive, cleared by display_flush() */
static bool s_dirty = false;
static bool s_ready = false;

/* FSMC bank 1 addresses for NumWorks LCD: A16 is the RS (D/C) pin */
#define LCD_CMD  (*(volatile uint16_t *)0x60000000UL)
#define LCD_DATA (*(volatile uint16_t *)0x60020000UL)

static void lcd_cmd(uint8_t cmd) { LCD_CMD = cmd; }
static void lcd_data(uint16_t d) { LCD_DATA = d; }
static void lcd_data8(uint8_t d) { LCD_DATA = d; }

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_cmd(0x2A);              /* Column address set */
    lcd_data8(x0 >> 8); lcd_data8(x0);
    lcd_data8(x1 >> 8); lcd_data8(x1);
    lcd_cmd(0x2B);              /* Row address set */
    lcd_data8(y0 >> 8); lcd_data8(y0);
    lcd_data8(y1 >> 8); lcd_data8(y1);
    lcd_cmd(0x2C);              /* Memory write */
}

void display_init(void) {
    /* Enable FSMC clock */
    RCC->AHB3ENR |= (1U << 0);   /* FMCEN (same bit on F730) */

    /* ILI9341 init sequence — 216 MHz PLL, FSMC configured
       by NumWorks bootloader; we just reset + configure here */
    lcd_cmd(0x01);  /* SW reset */
    for (volatile int i = 0; i < 200000; i++) {}

    lcd_cmd(0x11);  /* Sleep out */
    for (volatile int i = 0; i < 200000; i++) {}

    lcd_cmd(0x3A); lcd_data8(0x55);   /* 16-bit colour */
    lcd_cmd(0x36); lcd_data8(0x48);   /* MADCTL: MX+BGR */
    lcd_cmd(0x29);                     /* Display on */

    memset(g_framebuf, 0x00, sizeof(g_framebuf));
    display_flush();
    s_ready = true;
}

bool display_ready(void) { return s_ready; }

/* ILI9341 display off + sleep in, and back. The backlight stays on:
 * its control pin hasn't been checked against a schematic yet. */
void display_power(bool on) {
    if (on) {
        lcd_cmd(0x11);                 /* sleep out: wait 120 ms */
        hal_delay_ms(120);
        lcd_cmd(0x29);                 /* display on */
        display_flush();
    } else {
        lcd_cmd(0x28);                 /* display off */
        lcd_cmd(0x10);                 /* sleep in: wait 5 ms */
        hal_delay_ms(5);
    }
}

/* Push entire framebuffer to LCD */
void display_flush(void) {
    lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1);
    const uint16_t *p   = g_framebuf;
    const uint16_t *end = g_framebuf + LCD_WIDTH * LCD_HEIGHT;
    while (p < end) { LCD_DATA = *p++; }
    s_dirty = false;
}

void display_update(void) {
    if (s_dirty) display_flush();
}

/* ── Drawing primitives ──────────────────────────────────────── */
void display_fill(uint16_t c) {
    uint32_t n = LCD_WIDTH * LCD_HEIGHT;
    uint16_t *p = g_framebuf;
    while (n--) *p++ = c;
    s_dirty = true;
}

void display_pixel(int16_t x, int16_t y, uint16_t c) {
    if ((unsigned)x < LCD_WIDTH && (unsigned)y < LCD_HEIGHT) {
        FB_PIX(x,y) = c;
        s_dirty = true;
    }
}

void display_hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
    if ((unsigned)y >= LCD_HEIGHT) return;
    for (int16_t i = 0; i < w; i++) display_pixel(x+i, y, c);
}

void display_vline(int16_t x, int16_t y, int16_t h, uint16_t c) {
    if ((unsigned)x >= LCD_WIDTH) return;
    for (int16_t i = 0; i < h; i++) display_pixel(x, y+i, c);
}

void display_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    display_hline(x,   y,   w, c);
    display_hline(x,   y+h-1, w, c);
    display_vline(x,   y,   h, c);
    display_vline(x+w-1, y, h, c);
}

void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    for (int16_t row = y; row < y+h; row++) display_hline(x, row, w, c);
}

void display_char(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg) {
    const uint8_t *bm = font_get_char(ch);
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = bm[row];
        for (int col = 0; col < FONT_W; col++) {
            uint16_t c = (bits & (0x80 >> col)) ? fg : bg;
            display_pixel(x+col, y+row, c);
        }
    }
}

/* Text that reaches the right edge continues on the next line, at the
 * same x it started at. */
void display_str(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg) {
    const int16_t x0 = x;
    while (*s) {
        if (x + FONT_W > LCD_WIDTH && x > x0) { x = x0; y += FONT_H + 2; }
        display_char(x, y, *s++, fg, bg);
        x += FONT_W + 1;
    }
}

void display_str_len(int16_t x, int16_t y, const char *s, int len,
                     uint16_t fg, uint16_t bg) {
    for (int i = 0; i < len && s[i]; i++) {
        display_char(x + i*(FONT_W+1), y, s[i], fg, bg);
    }
}
