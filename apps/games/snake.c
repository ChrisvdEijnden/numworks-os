/* ================================================================
 * NumWorks OS — Snake
 * File: apps/games/snake.c
 *
 * Arrows steer (no turning back on yourself); each apple makes the
 * snake longer and faster. Hitting a wall or yourself ends the game.
 * OK starts again; BACK returns to the Games menu.
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

#define CELL     10
#define OY       28                        /* playfield top */
#define OX       0
#define CELLS    (SNAKE_W * SNAKE_H)
#define C_BG     RGB(10,10,20)
#define C_FIELD  RGB(16,24,16)
#define C_SNAKE  RGB(80,220,80)
#define C_HEAD   RGB(180,255,140)
#define C_APPLE  RGB(255,60,60)

static uint16_t s_body[CELLS];             /* ring buffer of cells, head at s_head */
static int      s_head, s_len;
static bool     s_occupied[CELLS];
static dir_t    s_dir, s_next_dir;
static int      s_apple;
static uint32_t s_score, s_rng, s_last_step, s_step_ms;
static bool     s_started, s_over, s_new_best;

static uint32_t rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }
static int cx(int c) { return c % SNAKE_W; }
static int cy(int c) { return c / SNAKE_W; }

static void draw_cell(int c, uint16_t col) {
    display_fill_rect(OX + cx(c) * CELL + 1, OY + cy(c) * CELL + 1, CELL - 2, CELL - 2, col);
}
static void clear_cell(int c) { display_fill_rect(OX + cx(c) * CELL, OY + cy(c) * CELL, CELL, CELL, C_FIELD); }

static void place_apple(void) {
    if (s_len >= CELLS) { s_apple = -1; return; }
    int c;
    do c = (int)(rnd() % CELLS); while (s_occupied[c]);
    s_apple = c;
    draw_cell(c, C_APPLE);
}

static void draw_status(void) {
    display_fill_rect(0, 0, LCD_WIDTH, OY - 2, RGB(30,120,60));
    char b[48];
    snprintf(b, sizeof b, TR("Snake  score %lu  record %lu", "Snake  score %lu  best %lu"),
             (unsigned long)s_score, (unsigned long)g_prefs.best[BEST_SNAKE]);
    display_str(6, 9, b, WHITE, RGB(30,120,60));
}

static void message(const char *a, const char *b) {
    display_fill_rect(40, 100, 240, 44, RGB(30,30,60));
    display_rect(40, 100, 240, 44, RGB(140,140,200));
    display_str(52, 108, a, WHITE, RGB(30,30,60));
    if (b) display_str(52, 124, b, YELLOW, RGB(30,30,60));
}

void snake_redraw(void) {
    display_fill(C_BG);
    draw_status();
    display_fill_rect(OX, OY, SNAKE_W * CELL, SNAKE_H * CELL, C_FIELD);
    for (int i = 0; i < s_len; i++) {
        int c = s_body[(s_head - i + CELLS) % CELLS];
        draw_cell(c, i == 0 ? C_HEAD : C_SNAKE);
    }
    if (s_apple >= 0) draw_cell(s_apple, C_APPLE);
    if (!s_started) message(TR("Druk op een pijl om te starten", "Press an arrow to start"), NULL);
    if (s_over) message(TR("GAME OVER  OK:Opnieuw", "GAME OVER  OK:Again"),
                        s_new_best ? TR("Nieuw record!", "New best score!") : NULL);
}

void snake_init(void) {
    memset(s_occupied, 0, sizeof s_occupied);
    s_rng ^= hal_tick_us();
    s_len = 3; s_head = 2;
    int start = (SNAKE_H / 2) * SNAKE_W + SNAKE_W / 2 - 2;
    for (int i = 0; i < 3; i++) { s_body[i] = (uint16_t)(start + i); s_occupied[start + i] = true; }
    s_dir = s_next_dir = DIR_RIGHT;
    s_score = 0;
    s_step_ms = 150;
    s_started = s_over = s_new_best = false;
    s_apple = -1;
    int c;
    do c = (int)(rnd() % CELLS); while (s_occupied[c]);
    s_apple = c;
}

static void step(void) {
    s_dir = s_next_dir;
    int h = s_body[s_head], x = cx(h), y = cy(h);
    if (s_dir == DIR_UP) y--; else if (s_dir == DIR_DOWN) y++;
    else if (s_dir == DIR_LEFT) x--; else x++;
    int tail = s_body[(s_head - s_len + 1 + CELLS) % CELLS];
    bool eat = (y * SNAKE_W + x) == s_apple;
    /* The tail moves away this step, so the head may take its cell */
    bool hit = x < 0 || y < 0 || x >= SNAKE_W || y >= SNAKE_H ||
               (s_occupied[y * SNAKE_W + x] && (eat || y * SNAKE_W + x != tail));
    if (hit) {
        s_over = true;
        s_new_best = prefs_new_best(BEST_SNAKE, s_score);
        draw_status();
        snake_redraw();
        return;
    }
    int nc = y * SNAKE_W + x;
    draw_cell(h, C_SNAKE);
    if (!eat) { s_occupied[tail] = false; clear_cell(tail); }
    else { s_len++; s_score += 10; if (s_step_ms > 60) s_step_ms -= 4; }
    s_head = (s_head + 1) % CELLS;
    s_body[s_head] = (uint16_t)nc;
    s_occupied[nc] = true;
    draw_cell(nc, C_HEAD);
    if (eat) { place_apple(); draw_status(); }
}

void snake_tick(void) {
    if (!s_started || s_over) return;
    uint32_t now = hal_tick_ms();
    if (now - s_last_step < s_step_ms) return;
    s_last_step = now;
    step();
}

void snake_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    if (k == KEY_HOME || k == KEY_BACK) {
        prefs_new_best(BEST_SNAKE, s_score);         /* an unfinished game counts too */
        kernel_set_app(k == KEY_HOME ? APP_HOME : APP_GAMES);
        return;
    }
    if (s_over) {
        if (key_is_exe(k)) { snake_init(); snake_redraw(); }
        return;
    }
    dir_t d;
    if (k == KEY_UP) d = DIR_UP; else if (k == KEY_DOWN) d = DIR_DOWN;
    else if (k == KEY_LEFT) d = DIR_LEFT; else if (k == KEY_RIGHT) d = DIR_RIGHT;
    else return;
    if ((d + 2) % 4 == s_dir && s_started) return;    /* no reversing into yourself */
    s_next_dir = d;
    if (!s_started) { s_started = true; s_last_step = hal_tick_ms(); snake_redraw(); }
}

/* For tests */
int snake_length(void) { return s_len; }
bool snake_over(void) { return s_over; }
int snake_head(void) { return s_body[s_head]; }
void snake_put_apple(int c) { s_apple = c; }
void snake_step(void) { step(); }
