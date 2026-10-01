/* ================================================================
 * NumWorks OS — Home Screen (3x4 Icon Grid)
 * File: apps/home/home.c
 *
 * Icons row by row:
 *   Rekenmachine | Functies  | Vergelijkingen
 *   Python       | Bestanden | Shell
 *   Tetris       | Docs      | Instellingen
 *   Foto's       | Editor    | [spare]
 * ================================================================ */
#include "home.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../include/stdio.h"

#define C_BG     RGB(18,18,30)
#define C_HDR    RGB(30,80,200)
#define C_ICON   RGB(35,35,55)
#define C_SEL    RGB(60,130,255)
#define C_BD     RGB(70,70,110)
#define C_SBD    RGB(140,200,255)
#define C_LBL    RGB(220,220,255)

#define HEADER_H 28
#define PAD       4
#define IW       (LCD_WIDTH  / HOME_COLS)
#define IH       ((LCD_HEIGHT - HEADER_H) / HOME_ROWS)
#define GRID_SZ  (HOME_COLS * HOME_ROWS)
#define N_REAL   11

typedef struct { const char *label; app_state_t app; uint16_t dot; } item_t;
static const item_t ITEMS[GRID_SZ] = {
    {"Rekenmachine", APP_CALCULATOR,   RGB(0,200,150)  },
    {"Functies",     APP_FUNCTIONS,    RGB(80,180,255) },
    {"Vergelijking", APP_EQUATIONS,    RGB(255,160,60) },
    {"Python",       APP_PYTHON,       RGB(255,200,0)  },
    {"Bestanden",    APP_FILEMANAGER,  RGB(100,220,80) },
    {"Shell",        APP_SHELL,        RGB(200,80,200) },
    {"Tetris",       APP_TETRIS,       RGB(255,60,60)  },
    {"Docs",         APP_DOCS,         RGB(120,120,220)},
    {"Instellingen", APP_SETTINGS,     RGB(180,180,180)},
    {"Foto's",       APP_PHOTO_VIEWER, RGB(255,120,160)},
    {"Editor",       APP_TEXT_EDITOR,  RGB(240,240,140)},
    {"",             APP_COUNT,        C_ICON          },
};

static int s_cur = 0;

static void draw_icon(int i) {
    bool sel = (i == s_cur);
    int ox = (i % HOME_COLS) * IW;
    int oy = HEADER_H + (i / HOME_COLS) * IH;
    uint16_t bg  = sel ? C_SEL  : C_ICON;
    uint16_t bd  = sel ? C_SBD  : C_BD;
    uint16_t fg  = sel ? WHITE  : C_LBL;
    display_fill_rect(ox+PAD, oy+PAD, IW-2*PAD, IH-2*PAD, bg);
    display_rect     (ox+PAD, oy+PAD, IW-2*PAD, IH-2*PAD, bd);
    if (i >= N_REAL) return;
    display_fill_rect(ox + IW/2 - 5, oy + PAD + 6, 11, 11, ITEMS[i].dot);
    display_str(ox+PAD+3, oy+IH-14, ITEMS[i].label, fg, bg);
}

void home_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(10, 8, "NumWorks OS", WHITE, C_HDR);
    display_str(LCD_WIDTH-96, 8, "EXE:Open", RGB(200,220,255), C_HDR);
    for (int i = 0; i < GRID_SZ; i++) draw_icon(i);
}

void home_init(void) { s_cur = 0; }

void home_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    int prev = s_cur;

    if      (k == KEY_RIGHT && (s_cur % HOME_COLS) < HOME_COLS-1) s_cur++;
    else if (k == KEY_LEFT  && (s_cur % HOME_COLS) > 0)           s_cur--;
    else if (k == KEY_DOWN  && s_cur + HOME_COLS < GRID_SZ)       s_cur += HOME_COLS;
    else if (k == KEY_UP    && s_cur - HOME_COLS >= 0)             s_cur -= HOME_COLS;
    else if (key_is_exe(k)) {
        if (s_cur < N_REAL && ITEMS[s_cur].app != APP_COUNT)
            kernel_set_app(ITEMS[s_cur].app);
        return;
    }
    if (s_cur != prev) { draw_icon(prev); draw_icon(s_cur); }
}
