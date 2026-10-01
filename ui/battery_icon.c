/* ================================================================
 * NumWorks OS — battery symbol for header bars
 * File: ui/battery_icon.c
 * ================================================================ */
#include "battery_icon.h"
#include "../hal/display.h"
#include "../hal/battery.h"

void battery_icon_draw(int16_t x, int16_t y, uint16_t bg) {
    battery_level_t level = battery_level();
    bool usb = battery_usb_powered(), charging = usb && battery_charging();
    uint16_t frame = WHITE;
    uint16_t fill = level == BAT_EMPTY ? RED : level == BAT_LOW ? YELLOW : GREEN;

    display_fill_rect(x, y, BATTERY_ICON_W, 12, bg);
    display_rect(x, y, 22, 12, frame);                 /* body */
    display_fill_rect(x + 22, y + 3, 3, 6, frame);     /* tip */
    /* EMPTY shows a sliver, then one bar per level */
    int bars = (int)level;
    if (bars == 0) display_fill_rect(x + 2, y + 2, 2, 8, fill);
    for (int i = 0; i < bars; i++)
        display_fill_rect(x + 2 + i * 6, y + 2, 5, 8, fill);
    if (charging) {                                    /* lightning bolt */
        uint16_t c = YELLOW;
        display_fill_rect(x + 11, y + 1, 2, 5, c);
        display_fill_rect(x + 9,  y + 5, 6, 2, c);
        display_fill_rect(x + 10, y + 7, 2, 4, c);
    }
}
