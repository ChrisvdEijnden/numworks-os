#pragma once
#include <stdint.h>
#define FONT_W 6
#define FONT_H 8
const uint8_t *font_get_char(char c);

/* The smoothed fonts (ui/fonts.c, made by tools/fontgen.py): a cell of
 * w x h pixels a character, ASCII 32..126, 4 bits of coverage a pixel
 * (two pixels a byte, the left one in the high nibble, rows padded to
 * a whole byte) */
typedef struct {
    uint8_t w, h;
    const uint8_t *data;
} font_t;
extern const font_t font_small;   /* 7 x 14 */
extern const font_t font_large;   /* 10 x 18 */
