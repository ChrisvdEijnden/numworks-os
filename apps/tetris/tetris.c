
/* ================================================================
 * NumWorks OS — Tetris
 * File: apps/tetris/tetris.c
 *
 * Classic Tetris: 10×20 board, 7 tetrominoes, line clear scoring.
 * Board origin: x=60, y=20 (below the title bar), 10*10=100 wide,
 * 20*10=200 tall
 * ================================================================ */
#include "tetris.h"
#include "../../ui/lang.h"
#include "../settings/prefs.h"
#include "../../ui/theme.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../hal/hal.h"
#include "../../include/config.h"
#include <string.h>
#include <stdio.h>

#define BW    TETRIS_BOARD_W
#define BH    TETRIS_BOARD_H
#define CS    TETRIS_CELL_SZ   /* 10 px */
#define OX    60               /* board left pixel */
#define OY    (UI_TITLE_H + 2) /* board top pixel */

#define C_BG  T_WALL           /* the empty board */
#define C_BD  T_GRAY_MIDDLE

/* Piece colours */
static const uint16_t PCOL[8] = {
    T_WALL,
    T_TURQUOISE,              /* I */
    T_BLUE,                   /* J */
    T_ORANGE,                 /* L */
    T_YELLOW,                 /* O */
    T_GREEN,                  /* S */
    T_MAGENTA,                /* T */
    T_RED,                    /* Z */
};

/* Piece rotations: 4 shapes × 4 rotations × 4 cells (dx,dy) */
typedef struct { int8_t dx[4], dy[4]; } shape_t;
static const shape_t SHAPES[7][4] = {
    /* I */ {{{0,1,2,3},{0,0,0,0}},{{2,2,2,2},{-1,0,1,2}},{{0,1,2,3},{1,1,1,1}},{{1,1,1,1},{-1,0,1,2}}},
    /* J */ {{{0,0,1,2},{0,1,1,1}},{{1,2,1,1},{0,0,1,2}},{{0,1,2,2},{1,1,1,0}},{{1,1,1,0},{0,1,2,2}}},
    /* L */ {{{0,1,2,2},{1,1,1,0}},{{1,1,1,2},{0,1,2,2}},{{0,0,1,2},{0,1,1,1}},{{0,1,1,1},{0,0,1,2}}},
    /* O */ {{{0,1,0,1},{0,0,1,1}},{{0,1,0,1},{0,0,1,1}},{{0,1,0,1},{0,0,1,1}},{{0,1,0,1},{0,0,1,1}}},
    /* S */ {{{1,2,0,1},{0,0,1,1}},{{0,0,1,1},{0,1,1,2}},{{1,2,0,1},{1,1,2,2}},{{1,1,0,0},{0,1,1,2}}},
    /* T */ {{{1,0,1,2},{0,1,1,1}},{{1,1,1,2},{0,1,2,1}},{{0,1,2,1},{1,1,1,2}},{{0,0,0,1},{0,1,2,1}}},
    /* Z */ {{{0,1,1,2},{0,0,1,1}},{{1,1,0,0},{0,1,1,2}},{{0,1,1,2},{1,1,2,2}},{{1,1,0,0},{1,2,2,3}}},
};

static uint8_t s_board[BH][BW];
static int s_px, s_py, s_ptype, s_prot;
static int s_score, s_lines, s_level;
static bool s_game_over;
static uint32_t s_last_drop;
static uint32_t s_drop_ms;

/* Simple LCG RNG */
static uint32_t s_rng = 12345;
static int rand_piece(void) { s_rng=s_rng*1664525+1013904223; return (s_rng>>16)%7; }

static void draw_cell(int bx, int by, uint16_t col) {
    int px = OX + bx*CS, py = OY + by*CS;
    if (col == C_BG) {
        /* Erase the whole cell, border included, or moving pieces
         * leave a trail of outlines */
        display_fill_rect(px, py, CS, CS, C_BG);
        return;
    }
    display_fill_rect(px, py, CS, CS, col);
    display_rect(px, py, CS, CS, WHITE);
}

static bool piece_fits(int px, int py, int t, int r) {
    for (int i = 0; i < 4; i++) {
        int nx = px + SHAPES[t][r].dx[i];
        int ny = py + SHAPES[t][r].dy[i];
        if (nx<0||nx>=BW||ny<0||ny>=BH) return false;
        if (s_board[ny][nx]) return false;
    }
    return true;
}

static void stamp_piece(void) {
    for (int i = 0; i < 4; i++) {
        int nx = s_px + SHAPES[s_ptype][s_prot].dx[i];
        int ny = s_py + SHAPES[s_ptype][s_prot].dy[i];
        s_board[ny][nx] = (uint8_t)(s_ptype+1);
    }
}

static void clear_lines(void) {
    int cleared = 0;
    for (int y = BH-1; y >= 0; ) {
        bool full = true;
        for (int x = 0; x < BW; x++) if (!s_board[y][x]) { full=false; break; }
        if (full) {
            for (int yy=y; yy>0; yy--) memcpy(s_board[yy], s_board[yy-1], BW);
            memset(s_board[0], 0, BW);
            cleared++;
        } else y--;
    }
    static const int pts[] = {0,100,300,500,800};
    if (cleared > 4) cleared = 4;
    s_score += pts[cleared] * (s_level+1);
    s_lines += cleared;
    s_level  = s_lines / 10;
    /* Signed: from level 16 on, 600 - level*40 is negative */
    int drop = 600 - s_level*40;
    s_drop_ms = (uint32_t)(drop < 100 ? 100 : drop);
}

static void new_piece(void) {
    s_ptype = rand_piece(); s_prot = 0;
    s_px = BW/2 - 1; s_py = 0;
    if (!piece_fits(s_px, s_py, s_ptype, s_prot)) s_game_over = true;
}

static void draw_board(void) {
    for (int y=0;y<BH;y++) for (int x=0;x<BW;x++)
        draw_cell(x, y, s_board[y][x] ? PCOL[s_board[y][x]] : C_BG);
}

static void draw_piece(uint16_t col) {
    for (int i=0;i<4;i++) {
        int nx=s_px+SHAPES[s_ptype][s_prot].dx[i];
        int ny=s_py+SHAPES[s_ptype][s_prot].dy[i];
        draw_cell(nx, ny, col);
    }
}

/* Score, record, lines and level beside the board; the keys below */
static void draw_sidebar(void) {
    int16_t sx = OX + BW*CS + 12, y = OY;
    display_fill_rect(sx, OY, LCD_WIDTH - sx, LCD_HEIGHT - OY, WHITE);
    char buf[32];
    const char *labels[4] = { "Score", TR("Record", "Best"), TR("Lijnen", "Lines"), "Level" };
    long values[4] = { s_score, (long)g_prefs.best[BEST_TETRIS], s_lines, s_level };
    for (int i = 0; i < 4; i++, y = (int16_t)(y + 38)) {
        display_text(sx, y, labels[i], &font_small, T_GRAY_VDARK, WHITE);
        snprintf(buf, sizeof(buf), "%ld", values[i]);
        display_text(sx, (int16_t)(y + 14), buf, &font_large, T_TEXT, WHITE);
    }
    const char *keys[4] = { TR("L/R: beweeg", "L/R: move"), TR("UP: draai", "UP: rotate"),
                            TR("DOWN: snel", "DOWN: drop"), TR("BACK: spellen", "BACK: games") };
    for (int i = 0; i < 4; i++)
        display_text(sx, (int16_t)(y + 4 + i * 14), keys[i], &font_small, T_GRAY_DARK, WHITE);
}

static bool s_new_best;   /* this game set the record */

static void draw_game_over(void) {
    display_fill_rect(40, 98, 240, 48, WHITE);
    display_rect(40, 98, 240, 48, T_GRAY_MIDDLE);
    ui_text_center(LCD_WIDTH / 2, s_new_best ? 106 : 115, TR("GAME OVER  OK:opnieuw", "GAME OVER  OK:again"),
                   &font_small, T_TEXT, WHITE);
    if (s_new_best) ui_text_center(LCD_WIDTH / 2, 124, TR("Nieuw record!", "New best score!"), &font_small,
                                   T_ORANGE, WHITE);
}

/* The falling piece can't move down: fix it, clear lines, spawn the next */
static void lock_piece(void) {
    stamp_piece(); clear_lines(); new_piece();
    if (s_game_over) s_new_best = prefs_new_best(BEST_TETRIS, (uint32_t)s_score);
    draw_board();
    draw_piece(PCOL[s_ptype+1]);
    draw_sidebar();
    if (s_game_over) draw_game_over();
}

void tetris_redraw(void) {
    ui_title_bar("Tetris");
    ui_body(WHITE);
    /* Board border */
    display_rect(OX-1, OY-1, BW*CS+2, BH*CS+2, C_BD);
    draw_board();
    draw_piece(PCOL[s_ptype+1]);
    draw_sidebar();
    if (s_game_over) draw_game_over();
    /* Don't drop a row for the time spent in other apps */
    s_last_drop = hal_tick_ms();
}

void tetris_init(void) {
    memset(s_board, 0, sizeof(s_board));
    s_score=0; s_lines=0; s_level=0; s_game_over=false; s_new_best=false;
    s_drop_ms=600; s_last_drop=hal_tick_ms();
    new_piece();
}

/* Called by the kernel every loop while Tetris is shown: gravity */
void tetris_tick(void) {
    uint32_t now = hal_tick_ms();
    if (s_game_over || now - s_last_drop < s_drop_ms) return;
    s_last_drop = now;
    if (piece_fits(s_px, s_py+1, s_ptype, s_prot)) {
        draw_piece(C_BG);
        s_py++;
        draw_piece(PCOL[s_ptype+1]);
    } else {
        lock_piece();
    }
}

void tetris_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    uint32_t now = hal_tick_ms();
    /* Key timing is the only entropy we have: mix it into the RNG so
     * every game gets a different piece sequence */
    s_rng ^= hal_tick_us();

    if (k == KEY_HOME) { kernel_set_app(APP_HOME); return; }
    if (k == KEY_BACK) { kernel_set_app(APP_GAMES); return; }
    if (s_game_over) {
        if (key_is_exe(k)) { tetris_init(); tetris_redraw(); }
        return;
    }

    draw_piece(C_BG);
    bool redraw = false;
    if (k == KEY_LEFT  && piece_fits(s_px-1,s_py,s_ptype,s_prot)) { s_px--; redraw=true; }
    else if (k == KEY_RIGHT && piece_fits(s_px+1,s_py,s_ptype,s_prot)) { s_px++; redraw=true; }
    else if (k == KEY_DOWN) {
        if (piece_fits(s_px,s_py+1,s_ptype,s_prot)) { s_py++; s_last_drop=now; redraw=true; }
        else { lock_piece(); return; }
    } else if (k == KEY_UP) {
        int nr = (s_prot+1)%4;
        if (piece_fits(s_px,s_py,s_ptype,nr)) { s_prot=nr; redraw=true; }
    } else if (key_is_exe(k)) {
        /* Hard drop */
        while (piece_fits(s_px,s_py+1,s_ptype,s_prot)) s_py++;
        lock_piece();
        return;
    }
    /* Redraw the piece whether or not it moved: it was erased above */
    draw_piece(PCOL[s_ptype+1]);
    if (redraw) draw_sidebar();
}
