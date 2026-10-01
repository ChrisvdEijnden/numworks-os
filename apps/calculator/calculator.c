/* ================================================================
 * NumWorks OS — Calculator App (RPN/infix scientific calculator)
 * File: apps/calculator/calculator.c
 *
 * Displays a standard calculator layout on the 320x240 screen.
 * Expressions are evaluated natively by apps/common/expr.c, so the
 * calculator works without MicroPython.
 * ================================================================ */
#include "calculator.h"
#include "../common/expr.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../include/string.h"
#include "../../include/stdio.h"
#include "../../include/stdlib.h"
#include "../../include/math.h"

#define C_BG    RGB(10,10,20)
#define C_HDR   RGB(30,80,200)
#define C_DISP  RGB(20,20,35)
#define C_EXPR  RGB(100,180,255)
#define C_RES   WHITE
#define C_ERR   RED

#define DISP_H  60
#define HEADER_H 24

static char s_expr[128];
static int  s_elen = 0;
static char s_result[128];
static bool s_error = false;
static bool s_shift = false;
static bool s_alpha = false;

static void calc_redraw_display(void) {
    display_fill_rect(0, HEADER_H, LCD_WIDTH, DISP_H, C_DISP);
    display_str(4, HEADER_H + 4, s_expr, C_EXPR, C_DISP);
    uint16_t rc = s_error ? C_ERR : C_RES;
    display_str(4, HEADER_H + 26, s_result, rc, C_DISP);
    /* Mode indicator */
    char mode[16];
    snprintf(mode, sizeof(mode), "%s%s",
             s_shift ? "SHIFT " : "",
             s_alpha ? "ALPHA" : "");
    display_str(LCD_WIDTH-80, HEADER_H+4, mode, YELLOW, C_DISP);
}

static void evaluate(void) {
    if (s_elen == 0) { s_result[0] = 0; s_error = false; return; }
    double v;
    expr_status_t st = expr_eval(s_expr, 0.0, &v);
    if (st != EXPR_OK) {
        snprintf(s_result, sizeof(s_result), "Fout: %s", expr_error(st));
        s_error = true;
        return;
    }
    expr_format(v, s_result, sizeof(s_result));
    expr_set_ans(v);
    s_error = false;
}

void calculator_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(8, 6, "Rekenmachine", WHITE, C_HDR);
    display_str(LCD_WIDTH-60, 6, "HOME:Terug", RGB(180,200,255), C_HDR);
    calc_redraw_display();

    /* Key layout hint grid */
    const char *hints[] = {
        "7","8","9","/",
        "4","5","6","*",
        "1","2","3","-",
        "0",".","^","+",
        "sin","cos","tan","sqrt",
        "ln","log","(",")",
    };
    int nhints = (int)(sizeof(hints) / sizeof(hints[0]));
    int hx = 4, hy = HEADER_H + DISP_H + 8;
    int bw = 76, bh = 20, gap = 2;
    for (int i = 0; i < nhints; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = hx + col*(bw+gap);
        int y = hy + row*(bh+gap);
        display_fill_rect(x, y, bw, bh, RGB(40,40,60));
        display_rect(x, y, bw, bh, RGB(80,80,120));
        display_str(x+2, y+6, hints[i], RGB(220,220,255), RGB(40,40,60));
    }
    display_str(4, hy + 6*(bh+gap) + 2, "OK: =  SHIFT: inverse  Ans: vorige",
                YELLOW, C_BG);
}

void calculator_init(void) {
    memset(s_expr,   0, sizeof(s_expr));
    memset(s_result, 0, sizeof(s_result));
    s_elen = 0; s_error = false;
}

void calculator_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) {
        kernel_set_app(APP_HOME); return;
    }
    if (k == KEY_SHIFT) { s_shift = !s_shift; calc_redraw_display(); return; }
    if (k == KEY_ALPHA) { s_alpha = !s_alpha; calc_redraw_display(); return; }

    /* EXE = evaluate */
    if (key_is_exe(k)) { evaluate(); calc_redraw_display(); return; }

    /* Backspace */
    if (k == KEY_BACKSPACE) {
        if (s_elen > 0) s_expr[--s_elen] = 0;
        calc_redraw_display(); return;
    }

    /* Map keys to text: letters in ALPHA mode, everything else via expr */
    const char *ins = NULL;
    static char letter[2];
    char c = s_alpha ? key_to_char(k, s_shift, true) : 0;
    if (c) { letter[0] = c; letter[1] = 0; ins = letter; }
    else   ins = expr_key_text(k, s_shift);
    if (ins && s_elen + (int)strlen(ins) < 127) {
        strcat(s_expr, ins);
        s_elen = (int)strlen(s_expr);
        s_shift = false;
        calc_redraw_display();
    }
}
