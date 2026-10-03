/* NumWorks OS — simulator: the window's picture (sim_window.c) */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sim.h"

/* The picture is drawn at twice the size the window opens at (sharp
 * on a Retina screen); the window scales it */
#define SIM_WIN_W 800
#define SIM_WIN_H 1600

/* Draws the whole window into fb (SIM_WIN_W × SIM_WIN_H, 0xRRGGBB) */
void       sim_window_draw(uint32_t *fb);
key_code_t sim_window_key_at(int x, int y);

/* The screen alone, as it looks with the backlight */
void sim_screen_pixels(uint32_t out[SIM_PANEL_H][SIM_PANEL_W]);
bool sim_save_bmp(const char *path, const uint32_t *px, int w, int h, int stride);
