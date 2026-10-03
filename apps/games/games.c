/* ================================================================
 * NumWorks OS — Games menu (Spellen)
 * File: apps/games/games.c
 *
 * Tetris, Snake and 2048 with their best scores (kept in the settings
 * file, apps/settings/prefs.c). BACK in a game comes back here.
 * ================================================================ */
#include "games.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../ui/lang.h"
#include "../../ui/theme.h"
#include "../settings/prefs.h"
#include <stdio.h>

#define ROW_Y  (UI_TITLE_H + 12)
#define ROW_H  48

static const struct { const char *name; app_state_t app; best_t best; uint16_t colour; } GAMES[] = {
    { "Tetris", APP_TETRIS, BEST_TETRIS, T_RED    },
    { "Snake",  APP_SNAKE,  BEST_SNAKE,  T_GREEN  },
    { "2048",   APP_2048,   BEST_2048,   T_YELLOW },
};
#define N_GAMES (int)(sizeof(GAMES) / sizeof(GAMES[0]))

static int s_sel;

void games_redraw(void) {
    ui_title_bar(TR("Spellen", "Games"));
    ui_body(T_WALL);
    for (int i = 0; i < N_GAMES; i++) {
        int16_t y = (int16_t)(ROW_Y + i * ROW_H), x = UI_MARGIN, w = LCD_WIDTH - 2 * UI_MARGIN;
        char b[32];
        snprintf(b, sizeof b, TR("Record: %lu", "Best: %lu"), (unsigned long)g_prefs.best[GAMES[i].best]);
        ui_row2(x, y, w, ROW_H + 1, GAMES[i].name, b, NULL, i == s_sel);
        display_fill_rect((int16_t)(x + w - 26), (int16_t)(y + ROW_H / 2 - 6), 12, 12, GAMES[i].colour);
    }
    ui_text_center(LCD_WIDTH / 2, LCD_HEIGHT - 22, TR("OK: spelen", "OK: play"), &font_small, T_GRAY_VDARK,
                   T_WALL);
}

void games_init(void) { s_sel = 0; }

void games_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    if (k == KEY_HOME || k == KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k == KEY_UP && s_sel > 0) s_sel--;
    else if (k == KEY_DOWN && s_sel < N_GAMES - 1) s_sel++;
    else if (key_is_exe(k)) { kernel_set_app(GAMES[s_sel].app); return; }
    else return;
    games_redraw();
}
