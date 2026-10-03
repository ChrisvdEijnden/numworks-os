/* NumWorks OS — simulator: NumWorks' own simulator picture (sim_skin.c) */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../hal/keyboard.h"

#define SKIN_MAX_KEYS 48

typedef struct { key_code_t key; int x, y, w, h; } skin_key_t;

/* What layout.json says: rectangles on the picture as Epsilon's build
 * crops and resizes it (the "background" size) */
typedef struct {
    int bg_w, bg_h;
    int screen[4];
    int nkeys;
    skin_key_t keys[SKIN_MAX_KEYS];
} skin_layout_t;

/* The picture, ready to draw: scaled so the screen in it is 640 x 480
 * (2 x 2 per pixel), on a white page; everything in its pixels */
typedef struct {
    int w, h;
    uint32_t *px;                       /* 0xRRGGBB */
    int scr_x, scr_y;
    int led_x, led_y;
    int nkeys;
    skin_key_t keys[SKIN_MAX_KEYS];
} sim_skin_t;

/* Reads layout.json's text; false if it isn't one */
bool sim_skin_parse_layout(const char *json, skin_layout_t *out);

/* Loads background-with-shadow.webp and layout.json from dir. False
 * when they aren't there, or the simulator was built without
 * SDL2_image (which reads the picture). */
bool sim_skin_load(const char *dir, sim_skin_t *out);

/* Whether this build can read the picture (it has SDL2_image) */
bool sim_skin_supported(void);
