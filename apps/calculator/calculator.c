/* ================================================================
 * NumWorks OS — Calculator App (RPN/infix scientific calculator)
 * File: apps/calculator/calculator.c
 *
 * Displays a standard calculator layout on the 320x240 screen.
 * Uses MicroPython's math module for trig/log/sqrt evaluation.
 * For full expression evaluation, expressions are passed to
 * MicroPython's eval() via mp_exec_eval_str().
 * ================================================================ */
#include "calculator.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../micropython-port/mp_port.h"
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
    if (s_elen == 0) { s_result[0] = 0; return; }
    /* Evaluate via MicroPython */
    char pycode[256];
    snprintf(pycode, sizeof(pycode),
             "import math,sys\n"
             "try:\n"
             "  _r=eval('%s')\n"
             "  print(_r)\n"
             "except Exception as e:\n"
             "  print('ERR:'+str(e))\n", s_expr);
    char out[64];
    int r = mp_exec_capture(pycode, out, sizeof(out));
    if (r >= 0 && strncmp(out, "ERR:", 4) == 0) {
        snprintf(s_result, sizeof(s_result), "Fout: %s", out+4);
        s_error = true;
    } else if (r >= 0) {
        strncpy(s_result, out, sizeof(s_result)-1);
        s_result[sizeof(s_result)-1] = 0;
        /* Remove trailing newline */
        int l = (int)strlen(s_result);
        while (l > 0 && (s_result[l-1] == '\n' || s_result[l-1] == '\r'))
            s_result[--l] = 0;
        s_error = false;
    } else {
        snprintf(s_result, sizeof(s_result), "eval mislukt");
        s_error = true;
    }
}

void calculator_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(8, 6, "Rekenmachine", WHITE, C_HDR);
    display_str(LCD_WIDTH-60, 6, "HOME:Terug", RGB(180,200,255), C_HDR);
    calc_redraw_display();

    /* Key layout hint grid */
    const char *hints[] = {
        "7","8","9","DEL",
        "4","5","6","*",
        "1","2","3","-",
        "0",".","=","+",
        "sin","cos","tan","sqrt",
        "ln","log","(",")",
        "pi","e","^","/"
    };
    int hx = 4, hy = HEADER_H + DISP_H + 8;
    int bw = 76, bh = 20, gap = 2;
    for (int i = 0; i < 28; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = hx + col*(bw+gap);
        int y = hy + row*(bh+gap);
        display_fill_rect(x, y, bw, bh, RGB(40,40,60));
        display_rect(x, y, bw, bh, RGB(80,80,120));
        display_str(x+2, y+6, hints[i], RGB(220,220,255), RGB(40,40,60));
    }
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
    if (k == KEY_EXE || k == KEY_OK) { evaluate(); calc_redraw_display(); return; }

    /* Backspace */
    if (k == KEY_BACKSPACE) {
        if (s_elen > 0) s_expr[--s_elen] = 0;
        calc_redraw_display(); return;
    }

    /* Map keys to characters */
    const char *ins = NULL;
    switch (k) {
        case KEY_0: ins="0"; break; case KEY_1: ins="1"; break;
        case KEY_2: ins="2"; break; case KEY_3: ins="3"; break;
        case KEY_4: ins="4"; break; case KEY_5: ins="5"; break;
        case KEY_6: ins="6"; break; case KEY_7: ins="7"; break;
        case KEY_8: ins="8"; break; case KEY_9: ins="9"; break;
        case KEY_DOT:   ins=".";    break;
        case KEY_PLUS:  ins="+";    break;
        case KEY_MINUS: ins="-";    break;
        case KEY_MUL:   ins="*";    break;
        case KEY_DIV:   ins="/";    break;
        case KEY_POW:   ins="**";   break;
        case KEY_SQRT:  ins = s_shift ? "math.cbrt(" : "math.sqrt("; break;
        case KEY_SIN:   ins = s_shift ? "math.asin(" : "math.sin(";  break;
        case KEY_COS:   ins = s_shift ? "math.acos(" : "math.cos(";  break;
        case KEY_TAN:   ins = s_shift ? "math.atan(" : "math.tan(";  break;
        case KEY_LN:    ins = s_shift ? "math.exp("  : "math.log(";  break;
        case KEY_LOG:   ins = s_shift ? "10**"       : "math.log10(";break;
        case KEY_EXP:   ins="math.e"; break;
        default:
            if (s_alpha) {
                char c = key_to_char(k, s_shift, true);
                if (c) { static char tmp[2]; tmp[0]=c; tmp[1]=0; ins=tmp; }
            }
            break;
    }
    if (ins && s_elen + (int)strlen(ins) < 127) {
        strcat(s_expr, ins);
        s_elen = (int)strlen(s_expr);
        calc_redraw_display();
        s_shift = false;
    }
}
