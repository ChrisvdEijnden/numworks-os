#pragma once
/* ================================================================
 * NumWorks OS — the look: colours and shared screen elements
 *
 * Modelled on NumWorks' own calculator software (its colours and
 * sizes; the code and drawings here are this OS's own): a yellow title
 * bar, light screens, lists of white rows, purple tabs.
 * ================================================================ */
#include <stdbool.h>
#include <stdint.h>
#include "../hal/display.h"

/* ── Colours ──────────────────────────────────────────────────── */
#define T_YELLOW       RGB(0xFF, 0xB7, 0x34)   /* title bar, selected app */
#define T_YELLOW_LIGHT RGB(0xFF, 0xEB, 0xC7)
#define T_PURPLE       RGB(0x65, 0x69, 0x75)   /* tab bar */
#define T_PURPLE_DARK  RGB(0x41, 0x41, 0x47)
#define T_GRAY_WHITE   RGB(0xF5, 0xF5, 0xF5)
#define T_GRAY_BRIGHT  RGB(0xEC, 0xEC, 0xEC)   /* row borders */
#define T_GRAY_MIDDLE  RGB(0xD9, 0xD9, 0xD9)   /* separators */
#define T_GRAY_DARK    RGB(0xA7, 0xA7, 0xA7)   /* secondary text */
#define T_GRAY_VDARK   RGB(0x8C, 0x8C, 0x8C)
#define T_GRAY_DARKEST RGB(0x33, 0x33, 0x33)
#define T_SELECT       RGB(0xD4, 0xD7, 0xE0)   /* selected row */
#define T_SELECT_DARK  RGB(0xB0, 0xB8, 0xD8)
#define T_WALL         RGB(0xF7, 0xF9, 0xFA)   /* behind lists and graphs */
#define T_WALL_DARK    RGB(0xE0, 0xE6, 0xED)
#define T_RED          RGB(0xFF, 0x00, 0x0C)
#define T_RED_LIGHT    RGB(0xFF, 0xCC, 0xCC)
#define T_BLUE         RGB(0x50, 0x75, 0xF2)
#define T_GREEN        RGB(0x50, 0xC1, 0x02)
#define T_ORANGE       RGB(0xFE, 0x87, 0x1F)
#define T_MAGENTA      RGB(0xFF, 0x05, 0x88)
#define T_TURQUOISE    RGB(0x60, 0xC1, 0xEC)
#define T_TEXT         RGB(0x00, 0x00, 0x00)

/* Curves and data series, in this order */
extern const uint16_t T_SERIES[6];

/* ── Sizes ────────────────────────────────────────────────────── */
#define UI_TITLE_H   18          /* title bar */
#define UI_TAB_H     27          /* tab bar below it */
#define UI_MARGIN    14          /* around lists */
#define UI_ROW_H  44          /* a list row with one line */

/* ── Text helpers ─────────────────────────────────────────────── */
void ui_text_center(int16_t cx, int16_t y, const char *s, const font_t *f, uint16_t fg, uint16_t bg);
void ui_text_right(int16_t right, int16_t y, const char *s, const font_t *f, uint16_t fg, uint16_t bg);

/* ── Title bar ────────────────────────────────────────────────────
 * "rad" on the left, the title in capitals in the middle, then shift
 * or alpha when they are on, and the battery. */
void ui_title_bar(const char *title);
void ui_title_bar_mods(const char *title, bool shift, bool alpha);
void ui_title_battery(void);              /* just the battery, again */
/* With other words left and right of the title (a game's score) */
void ui_title_bar_info(const char *title, const char *left, const char *right);

/* The screen below the title bar, in one colour */
void ui_body(uint16_t colour);

/* ── Tabs: names side by side on a purple bar at y ───────────────
 * The active one is white; with focus on the tabs it is shaded. */
void ui_tabs(int16_t y, const char *const *names, int n, int active, bool focused);

/* ── Lists ────────────────────────────────────────────────────────
 * A row of a list on the light background: white (shaded when
 * selected) with a light border, a label on the left in the large font
 * and, if given, a value on the right in grey. */
void ui_row(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, const char *value,
            bool selected);
/* The same, with a smaller second line under the label */
void ui_row2(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, const char *sub,
             const char *value, bool selected);

/* A thin scroll bar: total items, of which visible from first on.
 * Nothing is drawn when they all fit. */
void ui_scrollbar(int16_t x, int16_t y, int16_t h, int first, int visible, int total);

/* A message in the middle of the screen, on a light background */
void ui_message(const char *title, const char *line1, const char *line2, const char *line3);
