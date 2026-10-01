#pragma once
#include <stdint.h>

/* Battery symbol for a header bar: 26 x 12 pixels at (x, y) */
#define BATTERY_ICON_W 26
void battery_icon_draw(int16_t x, int16_t y, uint16_t bg);
