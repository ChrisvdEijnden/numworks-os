/* ================================================================
 * NumWorks OS — shared screen elements (see theme.h)
 * ================================================================ */
#include "theme.h"
#include "icons.h"
#include "lang.h"
#include "../hal/battery.h"
#include <ctype.h>
#include <string.h>

const uint16_t T_SERIES[6] = { T_RED, T_BLUE, T_GREEN, T_YELLOW, T_MAGENTA, T_TURQUOISE };

void ui_text_center(int16_t cx, int16_t y, const char *s, const font_t *f, uint16_t fg, uint16_t bg) {
    display_text((int16_t)(cx - display_text_width(s, f) / 2), y, s, f, fg, bg);
}

void ui_text_right(int16_t right, int16_t y, const char *s, const font_t *f, uint16_t fg, uint16_t bg) {
    display_text((int16_t)(right - display_text_width(s, f)), y, s, f, fg, bg);
}

/* ── Title bar ────────────────────────────────────────────────── */
#define BAT_W 15              /* the battery: body 13 x 8, tip 2 x 4 */
#define BAT_X (LCD_WIDTH - 5 - BAT_W)
#define BAT_Y 5

void ui_title_battery(void) {
    battery_level_t level = battery_level();
    bool charging = battery_usb_powered() && battery_charging();
    uint16_t fg = WHITE, fill = level == BAT_EMPTY && !battery_usb_powered() ? T_RED : WHITE;
    display_fill_rect(BAT_X - 1, 0, BAT_W + 2, UI_TITLE_H, T_YELLOW);
    display_rect(BAT_X, BAT_Y, 13, 8, fg);
    display_fill_rect(BAT_X + 13, BAT_Y + 2, 2, 4, fg);
    int w = level == BAT_EMPTY ? 2 : 3 * (int)level;            /* up to 9 of 9 pixels */
    display_fill_rect(BAT_X + 2, BAT_Y + 2, w, 4, fill);
    if (charging) {                                              /* a small bolt */
        display_fill_rect(BAT_X + 6, BAT_Y + 1, 2, 3, T_YELLOW);
        display_fill_rect(BAT_X + 4, BAT_Y + 3, 5, 2, T_YELLOW);
        display_fill_rect(BAT_X + 5, BAT_Y + 4, 2, 3, T_YELLOW);
    }
}

void ui_title_bar_info(const char *title, const char *left, const char *right) {
    char upper[40];
    size_t n = strlen(title);
    if (n >= sizeof upper) n = sizeof upper - 1;
    for (size_t i = 0; i < n; i++) upper[i] = (char)toupper((unsigned char)title[i]);
    upper[n] = 0;
    display_fill_rect(0, 0, LCD_WIDTH, UI_TITLE_H, T_YELLOW);
    if (left) display_text(5, 2, left, &font_small, WHITE, T_YELLOW);
    ui_text_center(LCD_WIDTH / 2, 2, upper, &font_small, WHITE, T_YELLOW);
    if (right) ui_text_right(BAT_X - 6, 2, right, &font_small, WHITE, T_YELLOW);
    ui_title_battery();
}

void ui_title_bar_mods(const char *title, bool shift, bool alpha) {
    ui_title_bar_info(title, "rad", alpha ? (shift ? "ALPHA" : "alpha") : shift ? "shift" : NULL);
}

void ui_title_bar(const char *title) { ui_title_bar_mods(title, false, false); }

void ui_body(uint16_t colour) {
    display_fill_rect(0, UI_TITLE_H, LCD_WIDTH, LCD_HEIGHT - UI_TITLE_H, colour);
}

/* ── Tabs ─────────────────────────────────────────────────────── */
void ui_tabs(int16_t y, const char *const *names, int n, int active, bool focused) {
    display_fill_rect(0, y, LCD_WIDTH, UI_TAB_H, T_PURPLE);
    int16_t w = (int16_t)(LCD_WIDTH / n);
    for (int i = 0; i < n; i++) {
        int16_t x = (int16_t)(i * w);
        bool on = i == active;
        uint16_t bg = on ? (focused ? T_SELECT : WHITE) : T_PURPLE;
        if (on) display_fill_rect(x, (int16_t)(y + 1), (int16_t)(i == n - 1 ? LCD_WIDTH - x : w),
                                  UI_TAB_H - 1, bg);
        ui_text_center((int16_t)(x + w / 2), (int16_t)(y + (UI_TAB_H - 14) / 2), names[i], &font_small,
                       on ? T_PURPLE : WHITE, bg);
    }
}

/* ── Lists ────────────────────────────────────────────────────── */
static void row_box(int16_t x, int16_t y, int16_t w, int16_t h, bool selected) {
    display_fill_rect(x, y, w, h, selected ? T_SELECT : WHITE);
    display_rect(x, y, w, h, T_GRAY_BRIGHT);
}

void ui_row(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, const char *value,
            bool selected) {
    uint16_t bg = selected ? T_SELECT : WHITE;
    row_box(x, y, w, h, selected);
    int16_t ty = (int16_t)(y + (h - 18) / 2);
    display_text((int16_t)(x + 10), ty, label, &font_large, T_TEXT, bg);
    if (value) ui_text_right((int16_t)(x + w - 10), (int16_t)(y + (h - 14) / 2), value, &font_small,
                             T_GRAY_VDARK, bg);
}

void ui_row2(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, const char *sub,
             const char *value, bool selected) {
    uint16_t bg = selected ? T_SELECT : WHITE;
    row_box(x, y, w, h, selected);
    int16_t ty = (int16_t)(y + (h - 18 - 14) / 2);
    display_text((int16_t)(x + 10), ty, label, &font_large, T_TEXT, bg);
    if (sub) display_text((int16_t)(x + 10), (int16_t)(ty + 18), sub, &font_small, T_GRAY_VDARK, bg);
    if (value) ui_text_right((int16_t)(x + w - 10), (int16_t)(y + (h - 14) / 2), value, &font_small,
                             T_GRAY_VDARK, bg);
}

void ui_scrollbar(int16_t x, int16_t y, int16_t h, int first, int visible, int total) {
    if (total <= visible || total <= 0) return;          /* all of it fits: no bar */
    display_fill_rect(x, y, 2, h, T_GRAY_MIDDLE);
    int16_t bh = (int16_t)(h * visible / total), by = (int16_t)(y + h * first / total);
    if (bh < 6) bh = 6;
    display_fill_rect(x, by, 2, bh, T_GRAY_DARKEST);
}

/* ── A message screen ─────────────────────────────────────────── */
void ui_message(const char *title, const char *line1, const char *line2, const char *line3) {
    ui_title_bar(title);
    ui_body(T_WALL);
    const char *lines[3] = { line1, line2, line3 };
    int n = 0;
    for (int i = 0; i < 3; i++) if (lines[i]) n++;
    int16_t y = (int16_t)(UI_TITLE_H + (LCD_HEIGHT - UI_TITLE_H - n * 22) / 2);
    for (int i = 0; i < 3; i++) {
        if (!lines[i]) continue;
        ui_text_center(LCD_WIDTH / 2, y, lines[i], i == 0 ? &font_large : &font_small,
                       i == 0 ? T_TEXT : T_GRAY_DARKEST, T_WALL);
        y = (int16_t)(y + 22);
    }
}

/* ── Icons: run-length coded, unpacked a row at a time ─────────── */
void ui_icon(const icon_t *icon, int16_t x, int16_t y) {
    uint16_t row[ICON_W];
    int col = 0, line = 0;
    for (uint16_t r = 0; r < icon->runs; r++) {
        uint16_t count = icon->rle[2 * r], colour = icon->rle[2 * r + 1];
        while (count--) {
            row[col++] = colour;
            if (col == ICON_W) {
                display_image(x, (int16_t)(y + line), ICON_W, 1, row);
                col = 0;
                line++;
            }
        }
    }
}
