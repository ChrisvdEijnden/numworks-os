/* ================================================================
 * NumWorks OS — Calculator App
 * File: apps/calculator/calculator.c
 *
 * Expressions are typed on the bottom line and evaluated natively by
 * apps/common/expr.c (no MicroPython needed). Every calculation goes
 * into a history above it, newest at the bottom:
 *   UP / DOWN     select an earlier calculation (DOWN past the newest
 *                 returns to the input line)
 *   LEFT / RIGHT  on a selected one: its expression or its result
 *   OK / EXE      copy the selected part into the input
 *   BACKSPACE     on a selected one: remove it from the history
 * The history survives leaving the app, not a restart.
 * ================================================================ */
#include "calculator.h"
#include "../common/expr.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../ui/lang.h"
#include "../../ui/theme.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define INPUT_H  37                                 /* the input bar at the bottom */
#define INPUT_Y  (LCD_HEIGHT - INPUT_H)
#define ROW_H    36                                 /* a calculation in the history */
#define HIST_ROWS ((INPUT_Y - UI_TITLE_H) / ROW_H)
#define PAD      10                                 /* left and right of the text */
#define CW       10                                 /* large font: a character's width */
#define COLS     ((LCD_WIDTH - 2 * PAD) / CW)       /* characters on a line */

#define EXPR_MAX 127
#define HIST_MAX CALC_HISTORY

typedef struct { char expr[EXPR_MAX + 1]; char result[24]; } entry_t;   /* whole expression: reused as is */

static entry_t s_hist[HIST_MAX];             /* oldest first */
static int  s_nhist;
static int  s_sel = -1;                      /* selected entry, -1: the input */
static bool s_sel_result = true;             /* which part of it */
static char s_expr[EXPR_MAX + 1];
static int  s_elen;
static char s_msg[48];                       /* an error, shown until the next key */
static bool s_shift, s_alpha;

/* ── Drawing ───────────────────────────────────────────────────── */
/* Text on a row, shaded when it is the selected part */
static void part(int16_t x, int16_t y, const char *t, bool selected) {
    int16_t w = (int16_t)display_text_width(t, &font_large);
    if (selected) display_fill_rect((int16_t)(x - 4), (int16_t)(y - 5), (int16_t)(w + 8), 28, T_SELECT);
    display_text(x, y, t, &font_large, T_TEXT, selected ? T_SELECT : WHITE);
}

static void draw_row(int row, int idx) {
    int16_t y = (int16_t)(INPUT_Y - (HIST_ROWS - row) * ROW_H);
    display_fill_rect(0, y, LCD_WIDTH, ROW_H, WHITE);
    if (idx < 0) return;
    display_hline(0, (int16_t)(y + ROW_H - 1), LCD_WIDTH, T_GRAY_MIDDLE);
    const entry_t *e = &s_hist[idx];
    bool sel = idx == s_sel;
    int rlen = (int)strlen(e->result);
    int room = COLS - rlen - 2;              /* characters left for the expression */
    char shown[COLS + 1];
    int elen = (int)strlen(e->expr);
    if (elen > room && room > 2) snprintf(shown, sizeof(shown), "%.*s..", room - 2, e->expr);
    else snprintf(shown, sizeof(shown), "%s", e->expr);
    int16_t ty = (int16_t)(y + (ROW_H - 18) / 2);
    part(PAD, ty, shown, sel && !s_sel_result);
    part((int16_t)(LCD_WIDTH - PAD - rlen * CW), ty, e->result, sel && s_sel_result);
}

/* The history rows, newest just above the input. When an older entry
 * is selected the list scrolls so it stays visible. */
static void draw_history(void) {
    display_fill_rect(0, UI_TITLE_H, LCD_WIDTH, INPUT_Y - UI_TITLE_H - HIST_ROWS * ROW_H, WHITE);
    int bottom = s_nhist - 1;                /* entry shown on the last row */
    if (s_sel >= 0 && s_sel < s_nhist - HIST_ROWS + 1) bottom = s_sel + HIST_ROWS - 1;
    for (int row = HIST_ROWS - 1, idx = bottom; row >= 0; row--, idx--)
        draw_row(row, idx >= 0 ? idx : -1);
    if (s_msg[0]) {                          /* the error, in a box in the middle */
        int16_t w = (int16_t)(display_text_width(s_msg, &font_small) + 32), h = 44;
        int16_t x = (int16_t)((LCD_WIDTH - w) / 2), y = (int16_t)(UI_TITLE_H + (INPUT_Y - UI_TITLE_H - h) / 2);
        display_fill_rect(x, y, w, h, WHITE);
        display_rect(x, y, w, h, T_GRAY_DARK);
        ui_text_center(LCD_WIDTH / 2, (int16_t)(y + 15), s_msg, &font_small, T_TEXT, WHITE);
    }
}

static void draw_input(void) {
    display_hline(0, INPUT_Y, LCD_WIDTH, T_GRAY_MIDDLE);
    display_fill_rect(0, INPUT_Y + 1, LCD_WIDTH, INPUT_H - 1, WHITE);
    /* The end of a long expression stays visible */
    const char *shown = s_elen > COLS - 1 ? s_expr + (s_elen - (COLS - 1)) : s_expr;
    int16_t ty = INPUT_Y + 10;
    display_text(PAD, ty, shown, &font_large, T_TEXT, WHITE);
    if (s_sel < 0)                           /* the cursor */
        display_fill_rect((int16_t)(PAD + (int)strlen(shown) * CW), (int16_t)(ty - 1), 1, 20, T_TEXT);
}

static void draw_title(void) {
    ui_title_bar_mods(TR("Rekenmachine", "Calculation"), s_shift, s_alpha);
}

void calculator_redraw(void) {
    draw_title();
    draw_history();
    draw_input();
}

/* ── History ───────────────────────────────────────────────────── */
static void hist_add(const char *expr, const char *result) {
    if (s_nhist == HIST_MAX) {                         /* drop the oldest */
        memmove(&s_hist[0], &s_hist[1], (HIST_MAX - 1) * sizeof(entry_t));
        s_nhist--;
    }
    entry_t *e = &s_hist[s_nhist++];
    snprintf(e->expr, sizeof(e->expr), "%s", expr);
    snprintf(e->result, sizeof(e->result), "%s", result);
}

static void hist_remove(int idx) {
    memmove(&s_hist[idx], &s_hist[idx + 1], (size_t)(s_nhist - idx - 1) * sizeof(entry_t));
    s_nhist--;
}

int calculator_history_count(void) { return s_nhist; }
const char *calculator_history_expr(int i)   { return i >= 0 && i < s_nhist ? s_hist[i].expr : NULL; }
const char *calculator_history_result(int i) { return i >= 0 && i < s_nhist ? s_hist[i].result : NULL; }
const char *calculator_input(void) { return s_expr; }

/* ── Input ─────────────────────────────────────────────────────── */
static void insert(const char *t) {
    int n = (int)strlen(t);
    if (s_elen + n > EXPR_MAX) return;
    memcpy(s_expr + s_elen, t, (size_t)n + 1);
    s_elen += n;
}

static void evaluate(void) {
    if (s_elen == 0) return;
    double v;
    expr_status_t st = expr_eval(s_expr, 0.0, &v);
    if (st != EXPR_OK) {                               /* keep the input to fix it */
        snprintf(s_msg, sizeof(s_msg), TR("Fout: %s", "Error: %s"), expr_error(st));
        return;
    }
    char res[24];
    expr_format(v, res, sizeof(res));
    expr_set_ans(v);
    hist_add(s_expr, res);
    s_expr[0] = 0;
    s_elen = 0;
}

void calculator_init(void) {
    s_nhist = 0;
    s_sel = -1;
    s_expr[0] = 0; s_elen = 0;
    s_msg[0] = 0;
    s_shift = s_alpha = false;
}

void calculator_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) {
        if (s_sel >= 0 && k == KEY_BACK) { s_sel = -1; calculator_redraw(); return; }
        kernel_set_app(APP_HOME); return;
    }

    /* Browsing the history */
    if (k == KEY_UP) {
        if (s_nhist == 0) return;
        if (s_sel < 0) { s_sel = s_nhist - 1; s_sel_result = true; }
        else if (s_sel > 0) s_sel--;
        s_msg[0] = 0;
        draw_history(); draw_input();
        return;
    }
    if (s_sel >= 0) {
        if (k == KEY_DOWN) {
            s_sel = s_sel + 1 < s_nhist ? s_sel + 1 : -1;
        } else if (k == KEY_LEFT || k == KEY_RIGHT) {
            s_sel_result = (k == KEY_RIGHT);
        } else if (key_is_exe(k)) {
            insert(s_sel_result ? s_hist[s_sel].result : s_hist[s_sel].expr);
            s_sel = -1;
        } else if (k == KEY_BACKSPACE) {
            hist_remove(s_sel);
            if (s_sel >= s_nhist) s_sel = s_nhist - 1;
        } else {
            return;
        }
        draw_history(); draw_input();
        return;
    }

    if (s_msg[0]) { s_msg[0] = 0; draw_history(); }   /* any key closes the error */
    if (k == KEY_SHIFT) { s_shift = !s_shift; draw_title(); return; }
    if (k == KEY_ALPHA) { s_alpha = !s_alpha; draw_title(); return; }
    if (k == KEY_DOWN) return;

    if (key_is_exe(k)) {
        evaluate();
        draw_history(); draw_input();
        return;
    }
    if (k == KEY_BACKSPACE) {
        if (s_elen > 0) s_expr[--s_elen] = 0;
        draw_input(); return;
    }

    /* Map keys to text: letters in ALPHA mode, everything else via expr */
    const char *ins = NULL;
    static char letter[2];
    char c = s_alpha ? key_to_char(k, s_shift, true) : 0;
    if (c) { letter[0] = c; letter[1] = 0; ins = letter; }
    else   ins = expr_key_text(k, s_shift);
    if (ins) {
        insert(ins);
        if (s_shift) { s_shift = false; draw_title(); }
        draw_input();
    }
}
