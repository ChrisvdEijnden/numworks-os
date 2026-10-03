/* The shared screen parts for suites that stub the display: they link
 * ui/theme.c and the fonts and icons with this, and define
 * display_text_n (where the text drawn is caught), display_fill_rect
 * and the lines. A full battery, no USB. */
#include <string.h>
#include "../../hal/display.h"
#include "../../hal/battery.h"

void display_text(int16_t x, int16_t y, const char *s, const font_t *f, uint16_t fg, uint16_t bg) {
    display_text_n(x, y, s, (int)strlen(s), f, fg, bg);
}
int display_text_width(const char *s, const font_t *f) { return (int)strlen(s) * f->w; }
void display_image(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *px) {
    (void)x; (void)y; (void)w; (void)h; (void)px;
}
battery_level_t battery_level(void) { return BAT_FULL; }
bool battery_usb_powered(void) { return false; }
bool battery_charging(void) { return false; }
