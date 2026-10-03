/* ================================================================
 * NumWorks OS — Home Screen
 * File: apps/home/home.c
 *
 * The apps as icons, three to a row, two rows on screen at a time; the
 * arrows move through them, OK or EXE opens one.
 * ================================================================ */
#include "home.h"
#include "../../ui/lang.h"
#include "../../ui/theme.h"
#include "../../ui/icons.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"

/* Three columns of 104 x 104 cells, two rows on screen at a time */
#define COLS     3
#define CELL     104
#define LEFT     4
#define TOP      (UI_TITLE_H + 4)
#define ROWS_SHOWN 2

typedef struct { const char *nl, *en; app_state_t app; const icon_t *icon; } item_t;
static const item_t ITEMS[] = {
    {"Rekenmachine", "Calculation",  APP_CALCULATOR,  &icon_calculation},
    {"Functies",     "Functions",    APP_FUNCTIONS,   &icon_functions},
    {"Vergelijking", "Equations",    APP_EQUATIONS,   &icon_equations},
    {"Statistiek",   "Statistics",   APP_STATISTICS,  &icon_statistics},
    {"Python",       "Python",       APP_PYTHON,      &icon_python},
    {"Bestanden",    "Files",        APP_FILEMANAGER, &icon_files},
    {"Editor",       "Editor",       APP_TEXT_EDITOR, &icon_editor},
    {"Shell",        "Shell",        APP_SHELL,       &icon_shell},
    {"Spellen",      "Games",        APP_GAMES,       &icon_games},
    {"Help",         "Help",         APP_DOCS,        &icon_help},
    {"Instellingen", "Settings",     APP_SETTINGS,    &icon_settings},
};
#define N_ITEMS ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))
#define N_ROWS  ((N_ITEMS + COLS - 1) / COLS)

static int s_cur = 0;       /* selected app */
static int s_top = 0;       /* first row on screen */

static void draw_cell(int i) {
    int row = i / COLS - s_top;
    if (row < 0 || row >= ROWS_SHOWN) return;
    int16_t x = (int16_t)(LEFT + (i % COLS) * CELL), y = (int16_t)(TOP + row * CELL);
    display_fill_rect(x, y, CELL, CELL, WHITE);
    if (i >= N_ITEMS) return;
    ui_icon(ITEMS[i].icon, (int16_t)(x + (CELL - ICON_W) / 2), (int16_t)(y + 12));
    const char *name = TR(ITEMS[i].nl, ITEMS[i].en);
    int16_t w = (int16_t)display_text_width(name, &font_small);
    int16_t tx = (int16_t)(x + (CELL - w) / 2), ty = (int16_t)(y + ICON_H + 20);
    bool sel = i == s_cur;
    if (sel) display_fill_rect((int16_t)(tx - 4), (int16_t)(ty - 1), (int16_t)(w + 8), 16, T_YELLOW);
    display_text(tx, ty, name, &font_small, sel ? WHITE : T_TEXT, sel ? T_YELLOW : WHITE);
}

static void draw_grid(void) {
    for (int r = 0; r < ROWS_SHOWN; r++)
        for (int c = 0; c < COLS; c++) draw_cell((s_top + r) * COLS + c);
    ui_scrollbar(LCD_WIDTH - 5, TOP + 6, ROWS_SHOWN * CELL - 12, s_top, ROWS_SHOWN, N_ROWS);
}

/* The title bar's battery, every few seconds */
void home_draw_status(void) { ui_title_battery(); }

void home_redraw(void) {
    ui_title_bar(TR("Applicaties", "Applications"));
    ui_body(WHITE);
    draw_grid();
}

void home_init(void) { s_cur = 0; s_top = 0; }

void home_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    int prev = s_cur;

    if      (k == KEY_RIGHT && s_cur + 1 < N_ITEMS)      s_cur++;     /* on to the next row */
    else if (k == KEY_LEFT  && s_cur > 0)                s_cur--;
    else if (k == KEY_DOWN  && s_cur + COLS < N_ITEMS)   s_cur += COLS;
    else if (k == KEY_DOWN  && s_cur / COLS < N_ROWS - 1) s_cur = N_ITEMS - 1;
    else if (k == KEY_UP    && s_cur - COLS >= 0)        s_cur -= COLS;
    else if (key_is_exe(k)) {
        kernel_set_app(ITEMS[s_cur].app);
        return;
    }
    if (s_cur == prev) return;
    int row = s_cur / COLS;
    if (row < s_top || row >= s_top + ROWS_SHOWN) {          /* scroll a row */
        s_top = row < s_top ? row : row - ROWS_SHOWN + 1;
        draw_grid();
    } else {
        draw_cell(prev);
        draw_cell(s_cur);
    }
}
