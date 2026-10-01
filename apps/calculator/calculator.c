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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define C_BG    RGB(10,10,20)
#define C_HDR   RGB(30,80,200)
#define C_EXPR  RGB(100,180,255)
#define C_RES   WHITE
#define C_ERR   RED
#define C_SEL   RGB(50,50,90)
#define C_INPUT RGB(20,20,35)

#define HEADER_H 24
#define ROW_H    18
#define INPUT_Y  (LCD_HEIGHT - 44)          /* input box: 2 lines */
#define HINT_Y   (LCD_HEIGHT - 12)
#define HIST_ROWS ((INPUT_Y - HEADER_H - 2) / ROW_H)
#define COLS     (LCD_WIDTH / 8 - 1)         /* characters per line */

#define EXPR_MAX 127
#define HIST_MAX CALC_HISTORY

typedef struct { char expr[EXPR_MAX + 1]; char result[24]; } entry_t;   /* whole expression: reused as is */

static entry_t s_hist[HIST_MAX];             /* oldest first */
static int  s_nhist;
static int  s_sel = -1;                      /* selected entry, -1: the input */
static bool s_sel_result = true;             /* which part of it */
static char s_expr[EXPR_MAX + 1];
static int  s_elen;
static char s_msg[48];                       /* error under the input */
static bool s_shift, s_alpha;

/* ── Drawing ───────────────────────────────────────────────────── */
static void draw_row(int row, int idx) {
    int y = HEADER_H + 2 + row * ROW_H;
    bool sel = idx == s_sel;
    uint16_t bg = sel ? C_SEL : C_BG;
    display_fill_rect(0, y, LCD_WIDTH, ROW_H, bg);
    if (idx < 0) return;
    const entry_t *e = &s_hist[idx];
    int rlen = (int)strlen(e->result);
    int room = COLS - rlen - 2;              /* characters left for the expression */
    char shown[COLS + 1];
    int elen = (int)strlen(e->expr);
    if (elen > room && room > 2) snprintf(shown, sizeof(shown), "%.*s..", room - 2, e->expr);
    else snprintf(shown, sizeof(shown), "%s", e->expr);
    uint16_t ebg = (sel && !s_sel_result) ? RGB(80,80,160) : bg;
    uint16_t rbg = (sel && s_sel_result)  ? RGB(80,80,160) : bg;
    display_str(4, y + 5, shown, C_EXPR, ebg);
    display_str(LCD_WIDTH - 4 - rlen * 8, y + 5, e->result, C_RES, rbg);
}

/* The history rows, newest just above the input. When an older entry
 * is selected the list scrolls so it stays visible. */
static void draw_history(void) {
    int bottom = s_nhist - 1;                /* entry shown on the last row */
    if (s_sel >= 0 && s_sel < s_nhist - HIST_ROWS + 1) bottom = s_sel + HIST_ROWS - 1;
    for (int row = HIST_ROWS - 1, idx = bottom; row >= 0; row--, idx--)
        draw_row(row, idx >= 0 ? idx : -1);
}

static void draw_input(void) {
    display_fill_rect(0, INPUT_Y - 2, LCD_WIDTH, 2, RGB(60,60,90));
    display_fill_rect(0, INPUT_Y, LCD_WIDTH, LCD_HEIGHT - INPUT_Y - 14, C_INPUT);
    /* The end of a long expression stays visible */
    const char *shown = s_elen > COLS - 1 ? s_expr + (s_elen - (COLS - 1)) : s_expr;
    char line[COLS + 2];
    snprintf(line, sizeof(line), "%.*s%s", COLS - 1, shown, s_sel < 0 ? "_" : "");
    display_str(4, INPUT_Y + 3, line, WHITE, C_INPUT);
    if (s_msg[0]) display_str(4, INPUT_Y + 16, s_msg, C_ERR, C_INPUT);
    char mode[16];
    snprintf(mode, sizeof(mode), "%s%s", s_shift ? "SHIFT " : "", s_alpha ? "ALPHA" : "");
    if (mode[0]) display_str(LCD_WIDTH - 4 - (int)strlen(mode) * 8, INPUT_Y + 16, mode, YELLOW, C_INPUT);
}

static void draw_hint(void) {
    display_fill_rect(0, HINT_Y - 2, LCD_WIDTH, 14, C_BG);
    display_str(4, HINT_Y, s_sel < 0
        ? TR("EXE:=  UP:Geschiedenis  SHIFT:inverse", "EXE:=  UP:History  SHIFT:inverse")
        : TR("OK:Gebruik  L/R:Som/Uitkomst  <-:Wis", "OK:Use  L/R:Calc/Result  <-:Delete"),
        YELLOW, C_BG);
}

void calculator_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(8, 6, TR("Rekenmachine", "Calculator"), WHITE, C_HDR);
    display_str(LCD_WIDTH - 84, 6, TR("HOME:Terug", "HOME:Back"), RGB(180,200,255), C_HDR);
    draw_history();
    draw_input();
    draw_hint();
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
        draw_history(); draw_input(); draw_hint();
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
        draw_history(); draw_input(); draw_hint();
        return;
    }

    if (k == KEY_SHIFT) { s_shift = !s_shift; draw_input(); return; }
    if (k == KEY_ALPHA) { s_alpha = !s_alpha; draw_input(); return; }
    if (k == KEY_DOWN) return;

    if (key_is_exe(k)) {
        s_msg[0] = 0;
        evaluate();
        draw_history(); draw_input();
        return;
    }
    if (k == KEY_BACKSPACE) {
        if (s_elen > 0) s_expr[--s_elen] = 0;
        s_msg[0] = 0;
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
        s_shift = false;
        s_msg[0] = 0;
        draw_input();
    }
}
