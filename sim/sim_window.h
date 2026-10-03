/* NumWorks OS — simulator: the window's picture (sim_window.c) */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sim.h"

/* The picture: NumWorks' own simulator picture when its files are in
 * skin_dir (sim_skin.c), else a calculator drawn here; skin_dir NULL
 * for the drawn one. True when NumWorks' picture is used. Either way
 * the screen in it is 640 x 480, twice the size the window opens at
 * (sharp on a Retina screen); the window scales it. */
bool sim_window_init(const char *skin_dir);
int  sim_window_w(void);
int  sim_window_h(void);

/* Draws the whole window into fb (sim_window_w() x sim_window_h(),
 * 0xRRGGBB) */
void       sim_window_draw(uint32_t *fb);
key_code_t sim_window_key_at(int x, int y);
/* The key under the mouse, shaded as on the online simulator; a point
 * off the keys (or -1, -1) for none */
void       sim_window_hover(int x, int y);

/* The screen alone, as it looks with the backlight */
void sim_screen_pixels(uint32_t out[SIM_PANEL_H][SIM_PANEL_W]);
bool sim_save_bmp(const char *path, const uint32_t *px, int w, int h, int stride);
