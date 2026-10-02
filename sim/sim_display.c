/* ================================================================
 * NumWorks OS — simulator: the LCD panel
 *
 * hal/display.c is the real driver (a copy made by tests/gen_host.py,
 * with the FMC bus replaced by host_cmd()/host_data()). This is the
 * ST7789V at the other end: power and reset pins, sleep, display on,
 * inversion, the column/row window and the frame memory. What the
 * window shows is what the driver has actually sent, so a missed
 * update shows up here as on the calculator.
 *
 * Coordinates are taken as the OS writes them (MV set: x is the
 * column); how the panel sits in the case can't be simulated.
 * ================================================================ */
#include <stdio.h>
#include <string.h>
#include "fake_hw.h"
#include "sim.h"
#include "../include/config.h"

GPIO_TypeDef fake_ports[5];

enum { PORT_C = 2, PORT_E = 4 };

/* Written by the OS thread, read by the window thread: at worst the
 * window shows a frame that is being drawn, as a real panel would */
static volatile uint16_t s_ram[SIM_PANEL_H][SIM_PANEL_W];
static volatile bool s_powered, s_awake, s_on, s_inverted;
static int s_cmd = -1, s_argn, s_rd;
static uint8_t s_args[4];
static int s_xs, s_xe = SIM_PANEL_W - 1, s_ys, s_ye = SIM_PANEL_H - 1, s_x, s_y;

static void reset_state(void) {
    s_awake = false;
    s_on = false;
    s_inverted = false;
    s_xs = 0; s_xe = SIM_PANEL_W - 1;
    s_ys = 0; s_ye = SIM_PANEL_H - 1;
}

void host_pin(int port, int pin, int level) {
    if (port == PORT_C && pin == LCD_POWER_PIN) s_powered = level;
    if (port == PORT_E && pin == LCD_RESET_PIN && !level) reset_state();
}

void host_cmd(uint16_t c) {
    s_cmd = c & 0xFF;
    s_argn = 0;
    s_rd = 0;
    switch (s_cmd) {
    case 0x01: reset_state(); break;                    /* SWRESET */
    case 0x10: s_awake = false; break;                  /* SLPIN */
    case 0x11: s_awake = true; break;                   /* SLPOUT */
    case 0x20: s_inverted = false; break;               /* INVOFF */
    case 0x21: s_inverted = true; break;                /* INVON */
    case 0x28: s_on = false; break;                     /* DISPOFF */
    case 0x29: s_on = true; break;                      /* DISPON */
    case 0x2C: s_x = s_xs; s_y = s_ys; break;           /* RAMWR */
    }
}

void host_data(uint16_t d) {
    if (s_cmd == 0x2C) {                                 /* pixels, row by row */
        if (s_y <= s_ye && s_x < SIM_PANEL_W && s_y < SIM_PANEL_H) s_ram[s_y][s_x] = d;
        if (++s_x > s_xe) { s_x = s_xs; s_y++; }
        return;
    }
    if (s_argn < 4) s_args[s_argn++] = (uint8_t)d;
    if (s_cmd == 0x2A && s_argn == 4) {                  /* CASET */
        s_xs = s_args[0] << 8 | s_args[1];
        s_xe = s_args[2] << 8 | s_args[3];
    }
    if (s_cmd == 0x2B && s_argn == 4) {                  /* RASET */
        s_ys = s_args[0] << 8 | s_args[1];
        s_ye = s_args[2] << 8 | s_args[3];
    }
    if (s_cmd == 0x36 && s_argn == 1 && !(d & 0x20))     /* MADCTL */
        fprintf(stderr, "sim: MADCTL %02X without MV: the window assumes landscape\n", (unsigned)d & 0xFF);
}

/* RDDID: an ST7789V */
uint16_t host_read(void) {
    static const uint8_t id[] = { 0xFF, 0x85, 0x85, 0x52 };   /* dummy byte first */
    return s_cmd == 0x04 && s_rd < 4 ? id[s_rd++] : 0;
}

/* N0110 panels show the colours inverted unless INVON is set
 * (LCD_INVERT in config.h), so the simulated one does too */
void sim_panel_read(uint32_t out[SIM_PANEL_H][SIM_PANEL_W]) {
    bool lit = s_powered && s_awake && s_on;
    for (int y = 0; y < SIM_PANEL_H; y++)
        for (int x = 0; x < SIM_PANEL_W; x++) {
            uint16_t p = s_ram[y][x];
            if (!s_inverted) p = (uint16_t)~p;
            uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            out[y][x] = lit ? (r << 19 | (r >> 2) << 16 | g << 10 | (g >> 4) << 8 | b << 3 | b >> 2) : 0;
        }
}
