#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "fake_hw.h"
#include "../../hal/display.h"
static uint16_t panel[240][320];        /* what the LCD shows, indexed [row][col] */
static uint8_t madctl = 0, colmod = 0; static int awake = 0, on = 0;
static int cmd = -1, argn = 0; static uint16_t args[8];
static int xs, xe, ys, ye, cx, cy; static long pixels = 0, bad_window = 0, cmds_while_busy = 0;
static uint32_t now = 0, ready_at = 0;  /* fake clock; command allowed after ready_at */
static char seq[256]; static char uart[512];
static int rd_n = 0;
static int powered = 0, in_reset = 0, invon = 0, cmds_unpowered = 0, resets = 0;
GPIO_TypeDef fake_ports[5];
/* LCD control pins: PC8 power, PE1 RESX */
void host_pin(int port, int pin, int level) {
    if (port == 2 && pin == 8) { powered = level; if (!level) { awake = 0; on = 0; } }
    if (port == 4 && pin == 1) {
        if (!level) { in_reset = 1; awake = 0; on = 0; madctl = 0; invon = 0; }
        else if (in_reset) { in_reset = 0; if (powered) resets++; ready_at = now + 120; }  /* resets of a powered panel */
    }
}
void hal_delay_ms(uint32_t ms) { now += ms; }
void hal_uart_puts(const char *s) { strncat(uart, s, sizeof uart - strlen(uart) - 1); }
void host_cmd(uint16_t c) {
    if (now < ready_at) cmds_while_busy++;
    if (!powered || in_reset) cmds_unpowered++;
    cmd = c; argn = 0; rd_n = 0;
    char b[8]; snprintf(b, sizeof b, "%02X ", c); strncat(seq, b, sizeof seq - strlen(seq) - 1);
    switch (c) {
    case 0x01: awake = 0; on = 0; madctl = 0; ready_at = now + 5; break;
    case 0x11: awake = 1; ready_at = now + 5; break;
    case 0x10: awake = 0; ready_at = now + 5; break;
    case 0x29: on = 1; break;
    case 0x21: invon = 1; break;
    case 0x28: on = 0; break;
    case 0x2C: cx = xs; cy = ys;
               if (xe >= ((madctl & 0x20) ? 320 : 240) || ye >= ((madctl & 0x20) ? 240 : 320) || xs > xe || ys > ye) bad_window++;
               break;
    }
}
void host_data(uint16_t d) {
    if (cmd == 0x2C) {                     /* pixels, row by row (MV=1: x is the column) */
        if (cy <= ye && cx <= xe && cx < 320 && cy < 240) panel[cy][cx] = d;
        pixels++;
        if (++cx > xe) { cx = xs; cy++; }
        return;
    }
    if (argn < 8) args[argn++] = d & 0xFF;
    if (cmd == 0x2A && argn == 4) { xs = args[0] << 8 | args[1]; xe = args[2] << 8 | args[3]; }
    if (cmd == 0x2B && argn == 4) { ys = args[0] << 8 | args[1]; ye = args[2] << 8 | args[3]; }
    if (cmd == 0x36 && argn == 1) madctl = (uint8_t)args[0];
    if (cmd == 0x3A && argn == 1) colmod = (uint8_t)args[0];
}
uint16_t host_read(void) { static const uint16_t id[] = { 0xFF, 0x4E, 0x41, 0x01 }; return cmd == 0x04 && rd_n < 4 ? id[rd_n++] : 0; }
static int fails = 0;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); else { printf("  FAIL %s\n", m); fails++; } } while (0)
static int panel_matches(void) {
    for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++) if (panel[y][x] != FB_PIX(x, y)) return 0;
    return 1;
}
int main(void) {
    memset(panel, 0xAA, sizeof panel);
    display_init();
    CHECK(strstr(seq, "04 11 3A 36 21 29") == seq, "init: read ID, SLPOUT, COLMOD, MADCTL, INVON, DISPON");
    CHECK(resets == 1 && powered, "panel powered, then a hardware reset (RESX pulse)");
    CHECK(cmds_while_busy == 0 && cmds_unpowered == 0, "no command while unpowered or in reset; waits respected (120 ms, 5 ms)");
    CHECK(invon, "inversion on (N0110 panels)");
    CHECK(madctl == LCD_MADCTL && (madctl & 0x20) && colmod == 0x55, "MV set (320x240), 16-bit colour");
    CHECK(strstr(uart, "4E 41 01 (NumWorks panel)") != NULL, "display ID read and logged");
    CHECK(panel_matches() && pixels == 320 * 240, "whole screen cleared once");
    CHECK(bad_window == 0, "windows within 320x240");

    puts("partial updates:");
    pixels = 0;
    display_fill_rect(10, 20, 30, 5, 0x1234); display_flush();
    CHECK(pixels == 30 * 5 && panel_matches(), "a 30x5 rectangle sends 150 pixels");
    pixels = 0; display_flush();
    CHECK(pixels == 0, "nothing drawn: nothing sent");
    pixels = 0; display_str(100, 100, "Hi", 0xFFFF, 0x0000); display_flush();
    CHECK(pixels <= 13 * 8 && panel_matches(), "two characters: one small window");
    pixels = 0; display_pixel(5, 5, 1); display_pixel(300, 200, 2); display_flush();
    CHECK(panel_matches() && pixels == 296 * 196, "two far-apart pixels: their bounding box");
    srand(7); int ok = 1; long total = 0;
    for (int i = 0; i < 2000; i++) {
        int op = rand() % 6; int16_t x = rand() % 400 - 40, y = rand() % 300 - 30, w = rand() % 60, h = rand() % 40;
        uint16_t c = rand();
        if (op == 0) display_fill_rect(x, y, w, h, c);
        if (op == 1) display_pixel(x, y, c);
        if (op == 2) display_char(x, y, 'A' + rand() % 26, c, ~c);
        if (op == 3) display_rect(x, y, w, h, c);
        if (op == 4) display_text(x, y, "Ab1", rand() % 2 ? &font_small : &font_large, c, ~c);
        if (op == 5) { static uint16_t img[40 * 60]; for (int k = 0; k < 40 * 60; k++) img[k] = (uint16_t)(c + k);
                       display_image(x, y, w ? w : 1, h ? h : 1, img); }
        if (rand() % 3 == 0) { pixels = 0; display_flush(); total += pixels; if (!panel_matches()) ok = 0; }
    }
    display_flush();
    CHECK(ok && panel_matches(), "2000 random draws (partly off screen): LCD always equals the framebuffer");
    CHECK(bad_window == 0, "every window within 320x240");
    printf("       (%ld pixels sent; a full flush each time would have sent ~%ld)\n", total, 667L * 76800);

    puts("text in the smooth fonts:");
    display_fill_rect(0, 0, 40, 30, 0xFFFF);
    display_text(2, 2, "W", &font_large, 0x0000, 0xFFFF);
    int black = 0, grey = 0, white = 0;
    for (int y = 2; y < 2 + font_large.h; y++) for (int x = 2; x < 2 + font_large.w; x++) {
        uint16_t p = FB_PIX(x, y);
        if (p == 0x0000) black++; else if (p == 0xFFFF) white++; else grey++;
    }
    CHECK(black > 10 && grey > 4 && white > 50, "a glyph: solid inside, blended at its edges, background around it");
    CHECK(FB_PIX(2 + font_large.w, 2) == 0xFFFF && FB_PIX(1, 2) == 0xFFFF, "nothing drawn outside its cell");
    CHECK(display_text_width("abc", &font_small) == 3 * font_small.w, "width: characters times the cell width");
    pixels = 0; display_text(316, 236, "clip", &font_small, 0, 0xFFFF); display_flush();
    CHECK(panel_matches() && bad_window == 0, "text at the corner is clipped");

    puts("sleep:");
    seq[0] = 0;
    display_power(false);
    CHECK(strstr(seq, "28 10") == seq && !powered, "DISPOFF, SLPIN, then the panel's power off");
    CHECK(((fake_ports[3].MODER >> 28) & 3) == 3 && ((fake_ports[4].MODER >> 14) & 3) == 3, "bus pins parked while it's off");
    pixels = 0; cmds_unpowered = 0; display_power(true);
    CHECK(awake && on && invon && powered && resets == 2, "power back, reset, set up again");
    CHECK(pixels == 76800 && panel_matches() && cmds_while_busy == 0 && cmds_unpowered == 0, "full refresh after waking");
    CHECK(((fake_ports[3].MODER >> 28) & 3) == 2, "bus pins back on the FMC");
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
