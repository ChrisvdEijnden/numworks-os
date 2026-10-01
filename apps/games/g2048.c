/* ================================================================
 * NumWorks OS — 2048
 * File: apps/games/g2048.c
 *
 * Arrows slide every tile; two equal tiles that meet merge into one
 * (once per move), scoring its value. After each move that changed
 * the board a new 2 (or, one time in ten, a 4) appears. The game ends
 * when no move is left. OK starts a new game; BACK returns to Games.
 * ================================================================ */
#include "games.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../hal/hal.h"
#include "../../include/config.h"
#include "../../ui/lang.h"
#include "../settings/prefs.h"
#include <stdio.h>
#include <string.h>

#define TILE  48
#define GAP   4
#define BX    ((LCD_WIDTH - 4 * TILE - 5 * GAP) / 2)
#define BY    34
#define C_BG  RGB(10,10,20)

static uint8_t  s_board[4][4];
static uint32_t s_score, s_rng;
static bool     s_over, s_won, s_new_best;

/* One row slid towards index 0: returns points, sets *moved */
static int slide_line(uint8_t *v[4], bool *moved) {
    uint8_t out[4] = {0, 0, 0, 0};
    int n = 0, points = 0;
    bool merged = false;
    for (int i = 0; i < 4; i++) {
        if (!*v[i]) continue;
        if (n > 0 && !merged && out[n - 1] == *v[i]) {
            out[n - 1]++;
            points += 1 << out[n - 1];
            merged = true;
        } else {
            out[n++] = *v[i];
            merged = false;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (*v[i] != out[i]) *moved = true;
        *v[i] = out[i];
    }
    return points;
}

int g2048_slide(uint8_t b[4][4], dir_t d) {
    bool moved = false;
    int points = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t *line[4];
        for (int j = 0; j < 4; j++) {
            switch (d) {
            case DIR_LEFT:  line[j] = &b[i][j]; break;
            case DIR_RIGHT: line[j] = &b[i][3 - j]; break;
            case DIR_UP:    line[j] = &b[j][i]; break;
            default:        line[j] = &b[3 - j][i]; break;
            }
        }
        points += slide_line(line, &moved);
    }
    return moved ? points : -1;
}

bool g2048_can_move(uint8_t b[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!b[r][c]) return true;
            if (c < 3 && b[r][c] == b[r][c + 1]) return true;
            if (r < 3 && b[r][c] == b[r + 1][c]) return true;
        }
    return false;
}

static uint32_t rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

static void add_tile(void) {
    int empty[16], n = 0;
    for (int i = 0; i < 16; i++) if (!s_board[i / 4][i % 4]) empty[n++] = i;
    if (!n) return;
    int i = empty[rnd() % (uint32_t)n];
    s_board[i / 4][i % 4] = (rnd() % 10 == 0) ? 2 : 1;
}

static uint16_t tile_colour(int e) {
    static const uint16_t C[] = {
        RGB(60,58,50), RGB(238,228,218), RGB(237,224,200), RGB(242,177,121), RGB(245,149,99),
        RGB(246,124,95), RGB(246,94,59), RGB(237,207,114), RGB(237,204,97), RGB(237,200,80),
        RGB(237,197,63), RGB(237,194,46),
    };
    return e < 12 ? C[e] : RGB(60,58,200);
}

static void draw_tile(int r, int c) {
    int e = s_board[r][c], x = BX + GAP + c * (TILE + GAP), y = BY + GAP + r * (TILE + GAP);
    uint16_t bg = tile_colour(e);
    display_fill_rect(x, y, TILE, TILE, bg);
    if (!e) return;
    char t[12];
    snprintf(t, sizeof t, "%lu", 1UL << e);
    int w = (int)strlen(t) * 8;
    display_str(x + (TILE - w) / 2, y + TILE / 2 - 4, t, e <= 2 ? RGB(110,100,90) : WHITE, bg);
}

static void draw_score(void) {
    display_fill_rect(0, 0, LCD_WIDTH, 26, RGB(160,120,40));
    char b[48];
    snprintf(b, sizeof b, TR("2048  score %lu  record %lu", "2048  score %lu  best %lu"),
             (unsigned long)s_score, (unsigned long)g_prefs.best[BEST_2048]);
    display_str(6, 9, b, WHITE, RGB(160,120,40));
}

void g2048_redraw(void) {
    display_fill(C_BG);
    draw_score();
    display_fill_rect(BX, BY, 4 * TILE + 5 * GAP, 4 * TILE + 5 * GAP, RGB(120,110,100));
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) draw_tile(r, c);
    const char *msg = s_over ? TR("Geen zetten meer. OK:Opnieuw", "No moves left. OK:Again")
                    : s_won ? TR("2048! Speel door of OK:Opnieuw", "2048! Play on, or OK:Again")
                    : TR("Pijlen:Schuif  BACK:Spellen", "Arrows:Slide  BACK:Games");
    display_str(8, LCD_HEIGHT - 26, msg, YELLOW, C_BG);
    if (s_new_best) display_str(8, LCD_HEIGHT - 12, TR("Nieuw record!", "New best score!"), RGB(255,200,60), C_BG);
}

void g2048_init(void) {
    memset(s_board, 0, sizeof s_board);
    s_rng ^= hal_tick_us();
    s_score = 0;
    s_over = s_won = s_new_best = false;
    add_tile();
    add_tile();
}

void g2048_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    if (k == KEY_HOME || k == KEY_BACK) {
        prefs_new_best(BEST_2048, s_score);          /* an unfinished game counts too */
        kernel_set_app(k == KEY_HOME ? APP_HOME : APP_GAMES);
        return;
    }
    if (key_is_exe(k) && (s_over || s_won)) { g2048_init(); g2048_redraw(); return; }
    if (s_over) return;
    dir_t d;
    if (k == KEY_UP) d = DIR_UP; else if (k == KEY_DOWN) d = DIR_DOWN;
    else if (k == KEY_LEFT) d = DIR_LEFT; else if (k == KEY_RIGHT) d = DIR_RIGHT;
    else return;
    int p = g2048_slide(s_board, d);
    if (p < 0) return;                               /* nothing moved: no new tile */
    s_score += (uint32_t)p;
    add_tile();
    for (int i = 0; i < 16; i++) if (s_board[i / 4][i % 4] >= 11) s_won = true;
    if (!g2048_can_move(s_board)) {
        s_over = true;
        s_new_best = prefs_new_best(BEST_2048, s_score);
    }
    g2048_redraw();
}
