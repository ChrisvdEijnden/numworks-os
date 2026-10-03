/* ================================================================
 * NumWorks OS — ST7789V Display Driver
 * File: hal/display.c
 *
 * The LCD is a Sitronix ST7789V on the FMC bus, 16-bit 8080 interface:
 * bank 1 (NE1) at 0x6000_0000, with address line A16 as D/CX (command
 * at 0x6000_0000, data at 0x6002_0000). The controller's memory is 240
 * columns x 320 rows; the row/column exchange bit (MV) in MADCTL makes
 * it 320 x 240 (ST7789V datasheet §8.12, 9.1.28).
 *
 * Drawing goes into g_framebuf. Every primitive extends a "dirty"
 * rectangle, and display_flush() sends only that window to the LCD.
 * ================================================================ */
#include "display.h"
#include "font.h"
#include "hal.h"
#include "uart.h"
#include "../include/stm32f730.h"
#include <string.h>

/* Framebuffer — 320*240*2 = 153,600 bytes in .bss */
uint16_t g_framebuf[LCD_WIDTH * LCD_HEIGHT] __attribute__((section(".framebuf")));

/* The part of the framebuffer drawn since the last flush (inclusive) */
static bool    s_dirty = false;
static int16_t s_x0, s_y0, s_x1, s_y1;
static bool    s_ready = false;

#define LCD_CMD  (*(volatile uint16_t *)0x60000000UL)
#define LCD_DATA (*(volatile uint16_t *)0x60020000UL)

/* ST7789V commands (datasheet table 18) */
#define CMD_RDDID   0x04
#define CMD_SLPIN   0x10
#define CMD_SLPOUT  0x11
#define CMD_INVON   0x21
#define CMD_DISPOFF 0x28
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C
#define CMD_MADCTL  0x36
#define CMD_COLMOD  0x3A
#define COLMOD_16BIT 0x55        /* RGB565 over the 16-bit bus */

static void lcd_cmd(uint8_t cmd) { LCD_CMD = cmd; }
static void lcd_data8(uint8_t d) { LCD_DATA = d; }

/* ── FMC (memory bus) ─────────────────────────────────────────────
 * Bank 1 as asynchronous SRAM, 16 bits, separate read and write
 * timings (mode A). HCLK is 192 MHz, 5.21 ns per cycle. ST7789V 8080
 * timing (datasheet table 4): write cycle >= 66 ns, WRX low >= 15 ns;
 * frame-memory read cycle >= 450 ns, RDX low >= 355 ns. Each pulse
 * gets ~15 ns extra for the signal edges.
 *   write: ADDSET 6 + DATAST 6 (31 ns low) + 1 = 13 cycles = 68 ns
 *   read:  ADDSET 15 + DATAST 72 (375 ns low) = 87 cycles = 453 ns */
#define FMC_BCR1   (*(volatile uint32_t *)0xA0000000UL)
#define FMC_BTR1   (*(volatile uint32_t *)0xA0000004UL)
#define FMC_BWTR1  (*(volatile uint32_t *)0xA0000104UL)
#define BCR_MBKEN  (1U << 0)
#define BCR_MUXEN  (1U << 1)
#define BCR_MTYP   (3U << 2)
#define BCR_MWID   (3U << 4)
#define BCR_MWID16 (1U << 4)
#define BCR_FACCEN (1U << 6)
#define BCR_WREN   (1U << 12)
#define BCR_WAITEN (1U << 13)
#define BCR_EXTMOD (1U << 14)
#define TIMING(addset, datast, busturn) \
    ((uint32_t)(addset) | ((uint32_t)(datast) << 8) | ((uint32_t)(busturn) << 16))
_Static_assert(SYSCLK_HZ == 192000000UL, "FMC timings below are for HCLK = 192 MHz");

/* The FMC pins (AF12): D0-D15, NOE, NWE, NE1 and A16 */
static const struct { uint8_t port; uint8_t pin; } FMC_PINS[] = {
    {3,14},{3,15},{3,0},{3,1},{4,7},{4,8},{4,9},{4,10},          /* D0-D7  */
    {4,11},{4,12},{4,13},{4,14},{4,15},{3,8},{3,9},{3,10},       /* D8-D15 */
    {3,4},{3,5},{3,7},{3,11},                                    /* NOE NWE NE1 A16 */
};

static GPIO_TypeDef *gpio_port(uint8_t n) {
    return (GPIO_TypeDef *)(AHB1_BASE + 0x400UL * n);
}

/* While the panel is unpowered the bus pins are parked (analog), so
 * they can't feed it through its inputs */
static void fmc_pins(bool on) {
    for (unsigned i = 0; i < sizeof(FMC_PINS) / sizeof(FMC_PINS[0]); i++) {
        GPIO_TypeDef *p = gpio_port(FMC_PINS[i].port);
        if (on) gpio_af(p, FMC_PINS[i].pin, 12U, 2U);
        else    gpio_analog(p, FMC_PINS[i].pin);
    }
}

static void fmc_init(void) {
    RCC->AHB3ENR |= (1U << 0);                       /* FMCEN */
    RCC->AHB1ENR |= (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4);  /* GPIOB-E */
    (void)RCC->AHB1ENR;
    fmc_pins(true);

    FMC_BCR1 = (FMC_BCR1 & ~(BCR_MUXEN | BCR_MTYP | BCR_MWID | BCR_FACCEN | BCR_WAITEN))
             | BCR_MBKEN | BCR_MWID16 | BCR_WREN | BCR_EXTMOD;
    FMC_BTR1  = TIMING(15, 72, 0);     /* reads */
    FMC_BWTR1 = TIMING(6, 6, 0);       /* writes */
}

/* ── Controller ───────────────────────────────────────────────── */
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_cmd(CMD_CASET);
    lcd_data8(x0 >> 8); lcd_data8(x0);
    lcd_data8(x1 >> 8); lcd_data8(x1);
    lcd_cmd(CMD_RASET);
    lcd_data8(y0 >> 8); lcd_data8(y0);
    lcd_data8(y1 >> 8); lcd_data8(y1);
    lcd_cmd(CMD_RAMWR);
}

/* RDDID: a dummy read, then ID1-ID3. The panels in NumWorks
 * calculators report 4E xx xx (4E 41 01 and 4E 48 01 are known); a
 * bare ST7789V reports 85 85 52. All 00 or all FF means nothing
 * answered: check the FMC set-up and the panel's power. */
static void log_display_id(void) {
    lcd_cmd(CMD_RDDID);
    (void)LCD_DATA;
    uint8_t id[3];
    for (int i = 0; i < 3; i++) id[i] = (uint8_t)LCD_DATA;
    char msg[40] = "lcd: id ";
    char *p = msg + 8;
    for (int i = 0; i < 3; i++) {
        *p++ = "0123456789ABCDEF"[id[i] >> 4];
        *p++ = "0123456789ABCDEF"[id[i] & 15];
        *p++ = ' ';
    }
    *p = 0;
    hal_uart_puts(msg);
    bool none = (id[0] | id[1] | id[2]) == 0 || (id[0] & id[1] & id[2]) == 0xFF;
    hal_uart_puts(id[0] == 0x4E ? "(NumWorks panel)\n" :
                  id[0] == 0x85 && id[1] == 0x85 && id[2] == 0x52 ? "(ST7789V)\n" :
                  none ? "(no answer)\n" : "(unknown panel)\n");
}

static void mark_all(void) {
    s_dirty = true;
    s_x0 = 0; s_y0 = 0; s_x1 = LCD_WIDTH - 1; s_y1 = LCD_HEIGHT - 1;
}

/* Power the panel up, reset it and set it up (datasheet 7.4.5, 9.1).
 * Also used after sleep, when the panel was powered down. */
static void panel_start(void) {
    gpio_output(LCD_EXTC_PORT, LCD_EXTC_PIN, true);
    gpio_input(LCD_TE_PORT, LCD_TE_PIN, GPIO_PULL_NONE);
    gpio_output(LCD_RESET_PORT, LCD_RESET_PIN, true);
    gpio_output(LCD_POWER_PORT, LCD_POWER_PIN, true);
    hal_delay_ms(1);
    /* Hardware reset: RESX low >= 10 us, then 120 ms before commands
     * (the reset may have caught the panel out of sleep) */
    gpio_write(LCD_RESET_PORT, LCD_RESET_PIN, false);
    hal_delay_ms(1);
    gpio_write(LCD_RESET_PORT, LCD_RESET_PIN, true);
    hal_delay_ms(120);
    log_display_id();
    lcd_cmd(CMD_SLPOUT);
    hal_delay_ms(10);                          /* >= 5 ms (9.1.12) */
    lcd_cmd(CMD_COLMOD); lcd_data8(COLMOD_16BIT);
    lcd_cmd(CMD_MADCTL); lcd_data8(LCD_MADCTL);
#if LCD_INVERT
    lcd_cmd(CMD_INVON);
#endif
    lcd_cmd(CMD_DISPON);
}

void display_init(void) {
    fmc_init();
    panel_start();
    memset(g_framebuf, 0x00, sizeof(g_framebuf));
    mark_all();
    display_flush();
    s_ready = true;
}

bool display_ready(void) { return s_ready; }

/* Off: display off, sleep in, then cut the panel's power. On: the
 * panel is set up again and the framebuffer resent. The backlight is
 * switched separately (hal/backlight.c). */
void display_power(bool on) {
    if (on) {
        fmc_pins(true);
        panel_start();
        mark_all();
        display_flush();
    } else {
        lcd_cmd(CMD_DISPOFF);
        lcd_cmd(CMD_SLPIN);
        hal_delay_ms(5);                       /* sleep-in takes 5 ms (9.1.11) */
        fmc_pins(false);
        gpio_write(LCD_RESET_PORT, LCD_RESET_PIN, false);
        gpio_write(LCD_POWER_PORT, LCD_POWER_PIN, false);
    }
}

uint16_t display_get_pixel(int16_t x, int16_t y) {
    if (x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return 0;
    return FB_PIX(x, y);
}

/* Send the part drawn since the last flush */
void display_flush(void) {
    if (!s_dirty) return;
    lcd_set_window((uint16_t)s_x0, (uint16_t)s_y0, (uint16_t)s_x1, (uint16_t)s_y1);
    for (int16_t y = s_y0; y <= s_y1; y++) {
        const uint16_t *p = &FB_PIX(s_x0, y), *end = p + (s_x1 - s_x0 + 1);
        while (p < end) LCD_DATA = *p++;
    }
    s_dirty = false;
}

void display_update(void) {
    display_flush();
}

/* ── Drawing primitives ──────────────────────────────────────── */
/* Clip a rectangle to the screen and add it to the dirty area; false
 * if nothing of it is visible */
static bool clip_mark(int16_t *x, int16_t *y, int16_t *w, int16_t *h) {
    int32_t x0 = *x, y0 = *y, x1 = (int32_t)*x + *w - 1, y1 = (int32_t)*y + *h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LCD_WIDTH - 1)  x1 = LCD_WIDTH - 1;
    if (y1 > LCD_HEIGHT - 1) y1 = LCD_HEIGHT - 1;
    if (x0 > x1 || y0 > y1) return false;
    if (!s_dirty) {
        s_dirty = true;
        s_x0 = (int16_t)x0; s_y0 = (int16_t)y0; s_x1 = (int16_t)x1; s_y1 = (int16_t)y1;
    } else {
        if (x0 < s_x0) s_x0 = (int16_t)x0;
        if (y0 < s_y0) s_y0 = (int16_t)y0;
        if (x1 > s_x1) s_x1 = (int16_t)x1;
        if (y1 > s_y1) s_y1 = (int16_t)y1;
    }
    *x = (int16_t)x0; *y = (int16_t)y0;
    *w = (int16_t)(x1 - x0 + 1); *h = (int16_t)(y1 - y0 + 1);
    return true;
}

void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (w <= 0 || h <= 0 || !clip_mark(&x, &y, &w, &h)) return;
    for (int16_t row = y; row < y + h; row++) {
        uint16_t *p = &FB_PIX(x, row);
        for (int16_t i = 0; i < w; i++) p[i] = c;
    }
}

void display_fill(uint16_t c) {
    display_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, c);
}

void display_pixel(int16_t x, int16_t y, uint16_t c) {
    display_fill_rect(x, y, 1, 1, c);
}

void display_hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
    display_fill_rect(x, y, w, 1, c);
}

void display_vline(int16_t x, int16_t y, int16_t h, uint16_t c) {
    display_fill_rect(x, y, 1, h, c);
}

void display_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    display_hline(x,   y,   w, c);
    display_hline(x,   y+h-1, w, c);
    display_vline(x,   y,   h, c);
    display_vline(x+w-1, y, h, c);
}

void display_char(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg) {
    int16_t cx = x, cy = y, cw = FONT_W, chh = FONT_H;
    if (!clip_mark(&cx, &cy, &cw, &chh)) return;
    const uint8_t *bm = font_get_char(ch);
    for (int row = 0; row < FONT_H; row++) {
        int16_t py = (int16_t)(y + row);
        if (py < cy || py >= cy + chh) continue;
        uint8_t bits = bm[row];
        for (int col = 0; col < FONT_W; col++) {
            int16_t px = (int16_t)(x + col);
            if (px < cx || px >= cx + cw) continue;
            FB_PIX(px, py) = (bits & (0x80 >> col)) ? fg : bg;
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

/* ── Smoothed text ──────────────────────────────────────────────
 * Each pixel's coverage (0..15) picks one of 16 colours between the
 * background and the text colour, worked out once per call. */
static void blend_table(uint16_t fg, uint16_t bg, uint16_t lut[16]) {
    int fr = fg >> 11, fgr = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    int br = bg >> 11, bgr = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    for (int a = 0; a < 16; a++)
        lut[a] = (uint16_t)(((br + (fr - br) * a / 15) << 11) |
                            ((bgr + (fgr - bgr) * a / 15) << 5) |
                            (bb + (fb - bb) * a / 15));
}

static void draw_glyph(int16_t x, int16_t y, char ch, const font_t *f, const uint16_t lut[16]) {
    int16_t cx = x, cy = y, cw = f->w, chh = f->h;
    if (!clip_mark(&cx, &cy, &cw, &chh)) return;
    if ((unsigned char)ch < 32 || (unsigned char)ch > 126) ch = '?';
    int row_bytes = (f->w + 1) / 2;
    const uint8_t *g = f->data + (ch - 32) * f->h * row_bytes;
    for (int row = cy - y; row < cy - y + chh; row++) {
        const uint8_t *r = g + row * row_bytes;
        uint16_t *p = &FB_PIX(cx, y + row);
        for (int col = cx - x; col < cx - x + cw; col++) {
            uint8_t b = r[col >> 1];
            *p++ = lut[(col & 1) ? (b & 15) : (b >> 4)];
        }
    }
}

void display_text_n(int16_t x, int16_t y, const char *s, int len, const font_t *f,
                    uint16_t fg, uint16_t bg) {
    uint16_t lut[16];
    blend_table(fg, bg, lut);
    for (int i = 0; i < len && s[i]; i++, x = (int16_t)(x + f->w))
        draw_glyph(x, y, s[i], f, lut);
}

void display_text(int16_t x, int16_t y, const char *s, const font_t *f, uint16_t fg, uint16_t bg) {
    display_text_n(x, y, s, 0x7FFF, f, fg, bg);
}

int display_text_width(const char *s, const font_t *f) {
    return (int)strlen(s) * f->w;
}

void display_image(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *px) {
    int16_t cx = x, cy = y, cw = w, ch = h;
    if (w <= 0 || h <= 0 || !clip_mark(&cx, &cy, &cw, &ch)) return;
    for (int16_t row = cy; row < cy + ch; row++)
        memcpy(&FB_PIX(cx, row), px + (row - y) * w + (cx - x), (size_t)cw * sizeof(uint16_t));
}

void display_str_len(int16_t x, int16_t y, const char *s, int len,
                     uint16_t fg, uint16_t bg) {
    for (int i = 0; i < len && s[i]; i++) {
        display_char(x + i*(FONT_W+1), y, s[i], fg, bg);
    }
}
