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
#include "../settings/prefs.h"
#include <stdio.h>

#define C_BG  RGB(10,10,20)
#define C_HDR RGB(30,80,200)

static const struct { const char *name; app_state_t app; best_t best; uint16_t colour; } GAMES[] = {
    { "Tetris", APP_TETRIS, BEST_TETRIS, RGB(255,60,60)  },
    { "Snake",  APP_SNAKE,  BEST_SNAKE,  RGB(80,220,80)  },
    { "2048",   APP_2048,   BEST_2048,   RGB(240,190,60) },
};
#define N_GAMES (int)(sizeof(GAMES) / sizeof(GAMES[0]))

static int s_sel;

void games_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, 28, C_HDR);
    display_str(8, 8, TR("Spellen", "Games"), WHITE, C_HDR);
    display_str(LCD_WIDTH - 84, 8, TR("HOME:Terug", "HOME:Back"), RGB(200,200,220), C_HDR);
    for (int i = 0; i < N_GAMES; i++) {
        int y = 44 + i * 52;
        bool sel = i == s_sel;
        uint16_t bg = sel ? RGB(60,130,255) : RGB(35,35,55);
        display_fill_rect(16, y, LCD_WIDTH - 32, 44, bg);
        display_rect(16, y, LCD_WIDTH - 32, 44, sel ? RGB(140,200,255) : RGB(70,70,110));
        display_fill_rect(28, y + 16, 12, 12, GAMES[i].colour);
        display_str(52, y + 10, GAMES[i].name, WHITE, bg);
        char b[32];
        snprintf(b, sizeof b, TR("Record: %lu", "Best: %lu"), (unsigned long)g_prefs.best[GAMES[i].best]);
        display_str(52, y + 26, b, sel ? WHITE : GREY, bg);
    }
    display_str(8, LCD_HEIGHT - 14, TR("UP/DOWN:Kies  OK:Spelen", "UP/DOWN:Select  OK:Play"), YELLOW, C_BG);
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
