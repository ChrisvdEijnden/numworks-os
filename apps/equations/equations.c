
/* ================================================================
 * NumWorks OS — Equations App (Vergelijkingen)
 * File: apps/equations/equations.c
 *
 * Modes:
 *  1. Quadratic: ax^2 + bx + c = 0
 *     Discriminant, exact roots (including complex)
 *  2. Linear system: ax + by = c
 *                    dx + ey = f
 *     Exact solution via Cramer's rule
 *  3. Single equation: f(x) = 0, solved numerically
 * ================================================================ */
#include "equations.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../micropython-port/mp_port.h"
#include "../../include/config.h"
#include "../../include/string.h"
#include "../../include/stdio.h"
#include "../../include/stdlib.h"
#include "../../include/math.h"

#define C_BG  RGB(10,10,20)
#define C_HDR RGB(30,80,200)
#define C_FLD RGB(30,30,50)
#define C_SEL RGB(60,60,100)
#define HEADER_H 28

typedef enum { MODE_QUAD, MODE_LINEAR, MODE_SINGLE, MODE_COUNT } eq_mode_t;
static eq_mode_t s_mode = MODE_QUAD;

/* Quadratic fields */
static char s_quad_a[24]="1", s_quad_b[24]="0", s_quad_c[24]="0";
/* Linear system fields */
static char s_lin[6][24] = {"1","0","0","0","1","0"};
/* Single equation */
static char s_single[64] = "";

static int  s_field = 0;   /* active input field */
static char s_input[64] = "";
static int  s_ilen = 0;
static char s_result[8][64];
static int  s_nresult = 0;
static bool s_editing = false;

static void show_results(void) {
    int y = HEADER_H + 120;
    display_fill_rect(0, y, LCD_WIDTH, LCD_HEIGHT-y, C_BG);
    for (int i = 0; i < s_nresult && i < 6; i++) {
        display_str(6, y + i*14, s_result[i], WHITE, C_BG);
    }
}

static void solve_quad(void) {
    double a = atof(s_quad_a), b = atof(s_quad_b), c = atof(s_quad_c);
    double D = b*b - 4*a*c;
    s_nresult = 0;
    snprintf(s_result[s_nresult++], 64, "D = %.4g", D);
    if (a == 0) {
        if (b == 0) snprintf(s_result[s_nresult++], 64, "Geen oplossing");
        else snprintf(s_result[s_nresult++], 64, "x = %.6g", -c/b);
    } else if (D > 0) {
        double r1 = (-b + sqrt(D)) / (2*a);
        double r2 = (-b - sqrt(D)) / (2*a);
        snprintf(s_result[s_nresult++], 64, "x1 = %.6g", r1);
        snprintf(s_result[s_nresult++], 64, "x2 = %.6g", r2);
    } else if (D == 0) {
        snprintf(s_result[s_nresult++], 64, "x = %.6g  (dubbel)", -b/(2*a));
    } else {
        double re = -b/(2*a), im = sqrt(-D)/(2*a);
        snprintf(s_result[s_nresult++], 64, "x1 = %.4g + %.4gi", re, im);
        snprintf(s_result[s_nresult++], 64, "x2 = %.4g - %.4gi", re, im);
    }
    show_results();
}

static void solve_linear(void) {
    double a=atof(s_lin[0]),b=atof(s_lin[1]),c=atof(s_lin[2]);
    double d=atof(s_lin[3]),e=atof(s_lin[4]),f=atof(s_lin[5]);
    double det = a*e - b*d;
    s_nresult = 0;
    if (fabs(det) < 1e-12) {
        snprintf(s_result[s_nresult++], 64, "Geen unieke oplossing");
    } else {
        double x = (c*e - b*f) / det;
        double y = (a*f - c*d) / det;
        snprintf(s_result[s_nresult++], 64, "x = %.6g", x);
        snprintf(s_result[s_nresult++], 64, "y = %.6g", y);
    }
    show_results();
}

static void solve_single(void) {
    char code[512];
    snprintf(code, sizeof(code),
        "from umath import *\ntry:\n x=0.0\n for _ in range(50):\n"
        "  f=eval('%s')\n  if abs(f)<1e-9:break\n  h=1e-5\n  xph=x+h\n"
        "  fh=eval('%s'.replace('x','(x+'+str(h)+')'))\n"
        "  x=x-f/((fh-f)/h)\n print(x)\nexcept Exception as ex:\n print('ERR')\n",
        s_single, s_single);
    char out[32]; s_nresult = 0;
    mp_exec_capture(code, out, sizeof(out));
    snprintf(s_result[s_nresult++], 64, "x ≈ %s", out);
    show_results();
}

static void draw_quad(void) {
    const char *labels[] = {"a=","b=","c="};
    char *vals[] = {s_quad_a, s_quad_b, s_quad_c};
    display_str(6, HEADER_H+8, "ax^2 + bx + c = 0", YELLOW, C_BG);
    for (int i = 0; i < 3; i++) {
        int y = HEADER_H + 30 + i*26;
        uint16_t bg = (s_field == i && s_editing) ? C_SEL : C_FLD;
        display_fill_rect(4, y, LCD_WIDTH-8, 22, bg);
        char line[48];
        snprintf(line, sizeof(line), "%s %s%s", labels[i], vals[i],
                 (s_field==i&&s_editing)?"_":"");
        display_str(8, y+5, line, WHITE, bg);
    }
}

static void draw_linear(void) {
    display_str(6, HEADER_H+8, "Lineair stelsel 2x2", YELLOW, C_BG);
    const char *lbl[] = {"a=","b=","c=","d=","e=","f="};
    char *vals[] = {s_lin[0],s_lin[1],s_lin[2],s_lin[3],s_lin[4],s_lin[5]};
    for (int i = 0; i < 6; i++) {
        int col = i / 3, row = i % 3;
        int x = 4 + col*160, y = HEADER_H+30+row*26;
        uint16_t bg = (s_field==i && s_editing) ? C_SEL : C_FLD;
        display_fill_rect(x, y, 152, 22, bg);
        char line[32];
        snprintf(line, sizeof(line), "%s%s%s", lbl[i], vals[i], (s_field==i&&s_editing)?"_":"");
        display_str(x+4, y+5, line, WHITE, bg);
    }
    display_str(6, HEADER_H+112, "ax+by=c  /  dx+ey=f", RGB(180,180,180), C_BG);
}

void equations_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(6, 8, "Vergelijkingen", WHITE, C_HDR);
    /* Mode tabs */
    const char *tabs[] = {"Kwadratisch","Lineair","Enkelvoudig"};
    for (int i = 0; i < 3; i++) {
        uint16_t tc = (i==s_mode) ? WHITE : RGB(150,150,200);
        display_str(4 + i*107, HEADER_H+1, tabs[i], tc,
                    (i==s_mode) ? RGB(50,80,160) : C_BG);
    }
    switch(s_mode) {
        case MODE_QUAD:   draw_quad();   break;
        case MODE_LINEAR: draw_linear(); break;
        default:
            display_str(6, HEADER_H+30, "Vergelijking: f(x)=0", YELLOW, C_BG);
            display_fill_rect(4, HEADER_H+52, LCD_WIDTH-8, 22, s_editing?C_SEL:C_FLD);
            {char line[72]; snprintf(line,sizeof(line),"%s%s",s_single,s_editing?"_":"");
             display_str(8, HEADER_H+57, line, WHITE, s_editing?C_SEL:C_FLD);}
            break;
    }
    display_str(4, LCD_HEIGHT-14, "EXE:Oplossen  LEFT/RIGHT:Modus  HOME:Terug",
                YELLOW, C_BG);
    show_results();
}

void equations_init(void) { s_mode = MODE_QUAD; s_field = 0; s_editing = false; }

void equations_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k == KEY_LEFT  && !s_editing) { if(s_mode>0)s_mode--; else s_mode=MODE_COUNT-1; equations_redraw(); return; }
    if (k == KEY_RIGHT && !s_editing) { s_mode=(s_mode+1)%MODE_COUNT; equations_redraw(); return; }
    if (k == KEY_EXE && !s_editing) {
        if      (s_mode==MODE_QUAD)   solve_quad();
        else if (s_mode==MODE_LINEAR) solve_linear();
        else                          solve_single();
        return;
    }
    /* Field navigation */
    if (k == KEY_DOWN && !s_editing) {
        int max_fields = (s_mode==MODE_QUAD)?3:(s_mode==MODE_LINEAR)?6:1;
        s_field = (s_field+1)%max_fields; equations_redraw(); return;
    }
    if (k == KEY_UP && !s_editing) {
        int max_fields = (s_mode==MODE_QUAD)?3:(s_mode==MODE_LINEAR)?6:1;
        s_field = (s_field+max_fields-1)%max_fields; equations_redraw(); return;
    }
    if (k == KEY_ALPHA) { s_editing = !s_editing; equations_redraw(); return; }

    if (s_editing) {
        if (k == KEY_BACKSPACE) {
            int l = (int)strlen(s_input);
            if (l > 0) s_input[--l] = 0;
            equations_redraw(); return;
        }
        if (k == KEY_EXE) {
            /* Commit field value */
            if (s_mode == MODE_QUAD) {
                char *qa[] = {s_quad_a, s_quad_b, s_quad_c};
                if (s_field < 3) strncpy(qa[s_field], s_input, 23);
            } else if (s_mode == MODE_LINEAR) {
                strncpy(s_lin[s_field], s_input, 23);
            } else {
                strncpy(s_single, s_input, 63);
            }
            s_input[0] = 0; s_ilen = 0; s_editing = false;
            equations_redraw(); return;
        }
        /* Character input */
        char ch = key_to_char(k, false, false);
        if (!ch) {
            if (k == KEY_MINUS) ch = '-';
            else if (k == KEY_DOT) ch = '.';
            else if (k == KEY_0)   ch = '0';
            else if (k == KEY_1)   ch = '1';
            else if (k == KEY_2)   ch = '2';
            else if (k == KEY_3)   ch = '3';
            else if (k == KEY_4)   ch = '4';
            else if (k == KEY_5)   ch = '5';
            else if (k == KEY_6)   ch = '6';
            else if (k == KEY_7)   ch = '7';
            else if (k == KEY_8)   ch = '8';
            else if (k == KEY_9)   ch = '9';
        }
        if (ch && s_ilen < 63) {
            s_input[s_ilen++] = ch;
            s_input[s_ilen] = 0;
            equations_redraw();
        }
    }
}
