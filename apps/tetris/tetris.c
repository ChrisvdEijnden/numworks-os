
/* ================================================================
 * NumWorks OS — Tetris
 * File: apps/tetris/tetris.c
 *
 * Classic Tetris: 10×20 board, 7 tetrominoes, line clear scoring.
 * Board origin: x=60, y=20  (10*10=100 wide, 20*10=200 tall)
 * ================================================================ */
#include "tetris.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../hal/timer.h"
#include "../../hal/hal.h"
#include "../../include/config.h"
#include "../../include/string.h"
#include "../../include/stdio.h"

#define BW    TETRIS_BOARD_W
#define BH    TETRIS_BOARD_H
#define CS    TETRIS_CELL_SZ   /* 10 px */
#define OX    60               /* board left pixel */
#define OY    20               /* board top pixel */

#define C_BG  RGB(10,10,20)
#define C_BD  RGB(60,60,80)

/* Piece colours */
static const uint16_t PCOL[8] = {
    BLACK,
    RGB(0,220,220),   /* I */
    RGB(0,60,220),    /* J */
    RGB(220,140,0),   /* L */
    RGB(220,220,0),   /* O */
    RGB(0,200,60),    /* S */
    RGB(160,0,220),   /* T */
    RGB(220,0,0),     /* Z */
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
    display_fill_rect(px+1, py+1, CS-2, CS-2, col);
    display_rect(px, py, CS, CS, RGB(200,200,200));
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

static void draw_sidebar(void) {
    int sx = OX + BW*CS + 8;
    display_fill_rect(sx, OY, LCD_WIDTH-sx-2, 160, C_BG);
    char buf[32];
    snprintf(buf, sizeof(buf), "Score");  display_str(sx, OY,     buf, YELLOW, C_BG);
    snprintf(buf, sizeof(buf), "%d", s_score); display_str(sx, OY+12, buf, WHITE, C_BG);
    snprintf(buf, sizeof(buf), "Lijnen"); display_str(sx, OY+30,  buf, YELLOW, C_BG);
    snprintf(buf, sizeof(buf), "%d", s_lines); display_str(sx, OY+42, buf, WHITE, C_BG);
    snprintf(buf, sizeof(buf), "Level");  display_str(sx, OY+60,  buf, YELLOW, C_BG);
    snprintf(buf, sizeof(buf), "%d", s_level); display_str(sx, OY+72, buf, WHITE, C_BG);
    display_str(sx, OY+100, "Ctrl:", RGB(180,180,180), C_BG);
    display_str(sx, OY+112, "L/R:Beweg", RGB(160,160,160), C_BG);
    display_str(sx, OY+124, "UP:Draai", RGB(160,160,160), C_BG);
    display_str(sx, OY+136, "DOWN:Snel", RGB(160,160,160), C_BG);
    display_str(sx, OY+148, "HOME:Stop", RGB(160,160,160), C_BG);
}

static void draw_game_over(void) {
    display_fill_rect(40, 100, 240, 40, RGB(200,0,0));
    display_str(60, 108, "GAME OVER  OK:Opnieuw", WHITE, RGB(200,0,0));
}

/* The falling piece can't move down: fix it, clear lines, spawn the next */
static void lock_piece(void) {
    stamp_piece(); clear_lines(); new_piece();
    draw_board();
    draw_piece(PCOL[s_ptype+1]);
    draw_sidebar();
    if (s_game_over) draw_game_over();
}

void tetris_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0,0,LCD_WIDTH,18, RGB(30,80,200));
    display_str(6, 4, "Tetris", WHITE, RGB(30,80,200));
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
    s_score=0; s_lines=0; s_level=0; s_game_over=false;
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
    s_rng ^= hal_micros();

    if (k == KEY_HOME || k == KEY_BACK) { kernel_set_app(APP_HOME); return; }
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
