
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
#include "../../ui/lang.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../common/expr.h"
#include "../../include/config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
static bool s_shift   = false;

static void show_results(void) {
    int y = HEADER_H + 120;
    display_fill_rect(0, y, LCD_WIDTH, LCD_HEIGHT-y, C_BG);
    for (int i = 0; i < s_nresult && i < 6; i++) {
        display_str(6, y + i*14, s_result[i], WHITE, C_BG);
    }
}

static bool finite_(double v) { return !isnan(v) && !isinf(v); }

/* Evaluate a field ("1/3", "-sqrt(2)" ...); on failure, report it */
static bool field_value(const char *src, const char *label, double *out) {
    expr_status_t st = expr_eval(src, 0.0, out);
    if (st == EXPR_OK && finite_(*out)) return true;
    snprintf(s_result[s_nresult++], 64, TR("Fout in %s %s", "Error in %s %s"), label,
             st == EXPR_OK ? TR("geen getal", "not a number") : expr_error(st));
    return false;
}

static void solve_quad(void) {
    double a, b, c;
    s_nresult = 0;
    if (!field_value(s_quad_a, "a:", &a) || !field_value(s_quad_b, "b:", &b) ||
        !field_value(s_quad_c, "c:", &c)) { show_results(); return; }
    double D = b*b - 4*a*c;
    snprintf(s_result[s_nresult++], 64, "D = %.6g", D);
    if (a == 0) {
        if (b == 0) snprintf(s_result[s_nresult++], 64, c == 0 ? TR("Elke x is een oplossing", "Every x is a solution")
                                                                    : TR("Geen oplossing", "No solution"));
        else snprintf(s_result[s_nresult++], 64, "x = %.10g", -c/b);
    } else if (D > 0) {
        double r1 = (-b + sqrt(D)) / (2*a);
        double r2 = (-b - sqrt(D)) / (2*a);
        snprintf(s_result[s_nresult++], 64, "x1 = %.10g", r1);
        snprintf(s_result[s_nresult++], 64, "x2 = %.10g", r2);
    } else if (D == 0) {
        snprintf(s_result[s_nresult++], 64, TR("x = %.10g  (dubbel)", "x = %.10g  (double)"), -b/(2*a));
    } else {
        double re = -b/(2*a), im = fabs(sqrt(-D)/(2*a));
        snprintf(s_result[s_nresult++], 64, "x1 = %.6g + %.6gi", re, im);
        snprintf(s_result[s_nresult++], 64, "x2 = %.6g - %.6gi", re, im);
    }
    show_results();
}

static void solve_linear(void) {
    static const char *lbl[] = {"a:","b:","c:","d:","e:","f:"};
    double v[6];
    s_nresult = 0;
    for (int i = 0; i < 6; i++)
        if (!field_value(s_lin[i], lbl[i], &v[i])) { show_results(); return; }
    double a=v[0], b=v[1], c=v[2], d=v[3], e=v[4], f=v[5];
    double det = a*e - b*d;
    if (fabs(det) < 1e-12) {
        snprintf(s_result[s_nresult++], 64, "%s", TR("Geen unieke oplossing", "No unique solution"));
    } else {
        double x = (c*e - b*f) / det;
        double y = (a*f - c*d) / det;
        snprintf(s_result[s_nresult++], 64, "x = %.10g", x);
        snprintf(s_result[s_nresult++], 64, "y = %.10g", y);
    }
    show_results();
}

/* f(x) for the single-equation solver: NaN on any error */
static double f_single(double x) {
    double y;
    return expr_eval(s_single, x, &y) == EXPR_OK ? y : NAN;
}

/* Newton's method with a central-difference derivative, tried from a few
 * starting points so a bad first guess doesn't end the search. */
static void solve_single(void) {
    static const double starts[] = {0.0, 1.0, -1.0, 10.0, -10.0, 100.0, -100.0};
    s_nresult = 0;
    double probe;
    expr_status_t st = expr_eval(s_single, 0.0, &probe);
    if (st != EXPR_OK) {
        snprintf(s_result[s_nresult++], 64, TR("Fout: %s", "Error: %s"), expr_error(st));
        show_results();
        return;
    }
    for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
        double x = starts[i];
        for (int it = 0; it < 60; it++) {
            double fx = f_single(x);
            if (!finite_(fx)) break;
            if (fabs(fx) < 1e-12) break;
            double h  = 1e-6 * (1.0 + fabs(x));
            double df = (f_single(x + h) - f_single(x - h)) / (2.0 * h);
            if (!finite_(df) || df == 0.0) break;
            double step = fx / df;
            x -= step;
            if (fabs(step) < 1e-12 * (1.0 + fabs(x))) break;
        }
        double fx = f_single(x);
        if (finite_(x) && finite_(fx) && fabs(fx) < 1e-9) {
            snprintf(s_result[s_nresult++], 64, "x = %.10g", x);
            snprintf(s_result[s_nresult++], 64, "f(x) = %.3g", fx);
            show_results();
            return;
        }
    }
    snprintf(s_result[s_nresult++], 64, "%s", TR("Geen oplossing gevonden", "No solution found"));
    show_results();
}

/* Text currently in field i of this mode (the editor shows s_input) */
static char *field_text(int i) {
    if (s_mode == MODE_QUAD) {
        char *qa[] = {s_quad_a, s_quad_b, s_quad_c};
        return qa[i < 3 ? i : 0];
    }
    if (s_mode == MODE_LINEAR) return s_lin[i < 6 ? i : 0];
    return s_single;
}
static int field_cap(void) {
    return s_mode == MODE_SINGLE ? (int)sizeof(s_single) - 1 : 23;
}
/* The last `max` characters of s, so the end being typed stays visible */
static const char *tail(const char *s, int max) {
    int n = (int)strlen(s);
    return n > max ? s + (n - max) : s;
}
static int field_count(void) {
    return (s_mode==MODE_QUAD) ? 3 : (s_mode==MODE_LINEAR) ? 6 : 1;
}

static void draw_quad(void) {
    const char *labels[] = {"a=","b=","c="};
    char *vals[] = {s_quad_a, s_quad_b, s_quad_c};
    display_str(6, HEADER_H+8, "ax^2 + bx + c = 0", YELLOW, C_BG);
    for (int i = 0; i < 3; i++) {
        int y = HEADER_H + 30 + i*26;
        uint16_t bg = (s_field == i && s_editing) ? C_SEL : C_FLD;
        display_fill_rect(4, y, LCD_WIDTH-8, 22, bg);
        char line[72];
        bool ed = (s_field==i && s_editing);
        snprintf(line, sizeof(line), "%s %s%s", labels[i], tail(ed ? s_input : vals[i], 40),
                 ed ? "_" : "");
        display_str(8, y+5, line, WHITE, bg);
    }
}

static void draw_linear(void) {
    display_str(6, HEADER_H+8, TR("Lineair stelsel 2x2", "Linear system 2x2"), YELLOW, C_BG);
    const char *lbl[] = {"a=","b=","c=","d=","e=","f="};
    char *vals[] = {s_lin[0],s_lin[1],s_lin[2],s_lin[3],s_lin[4],s_lin[5]};
    for (int i = 0; i < 6; i++) {
        int col = i / 3, row = i % 3;
        int x = 4 + col*160, y = HEADER_H+30+row*26;
        uint16_t bg = (s_field==i && s_editing) ? C_SEL : C_FLD;
        display_fill_rect(x, y, 152, 22, bg);
        char line[72];
        bool ed = (s_field==i && s_editing);
        /* 152-px column: 21 characters incl. label and cursor */
        snprintf(line, sizeof(line), "%s%s%s", lbl[i], tail(ed ? s_input : vals[i], 18), ed ? "_" : "");
        display_str(x+4, y+5, line, WHITE, bg);
    }
    display_str(6, HEADER_H+112, "ax+by=c  /  dx+ey=f", RGB(180,180,180), C_BG);
}

void equations_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(6, 8, TR("Vergelijkingen", "Equations"), WHITE, C_HDR);
    /* Mode tabs */
    const char *tabs[] = {TR("Kwadratisch", "Quadratic"), TR("Lineair", "Linear"), TR("Enkelvoudig", "Single")};
    for (int i = 0; i < 3; i++) {
        uint16_t tc = (i==(int)s_mode) ? WHITE : RGB(150,150,200);
        display_str(4 + i*107, HEADER_H+1, tabs[i], tc,
                    (i==(int)s_mode) ? RGB(50,80,160) : C_BG);
    }
    switch(s_mode) {
        case MODE_QUAD:   draw_quad();   break;
        case MODE_LINEAR: draw_linear(); break;
        default:
            display_str(6, HEADER_H+30, TR("Vergelijking: f(x)=0", "Equation: f(x)=0"), YELLOW, C_BG);
            display_fill_rect(4, HEADER_H+52, LCD_WIDTH-8, 22, s_editing?C_SEL:C_FLD);
            {char line[72]; snprintf(line,sizeof(line),"%s%s",tail(s_editing?s_input:s_single,42),s_editing?"_":"");
             display_str(8, HEADER_H+57, line, WHITE, s_editing?C_SEL:C_FLD);}
            break;
    }
    display_str(4, LCD_HEIGHT-14, s_editing ? TR("OK:Klaar  XNT: x  ALPHA:Annuleer", "OK:Done  XNT: x  ALPHA:Cancel")
                                            : TR("OK:Los op  ALPHA:Bewerk  L/R:Modus", "OK:Solve  ALPHA:Edit  L/R:Mode"),
                YELLOW, C_BG);
    show_results();
}

void equations_init(void) { s_mode = MODE_QUAD; s_field = 0; s_editing = false; }

void equations_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k == KEY_LEFT  && !s_editing) { if(s_mode>0)s_mode--; else s_mode=MODE_COUNT-1; s_field=0; s_nresult=0; equations_redraw(); return; }
    if (k == KEY_RIGHT && !s_editing) { s_mode=(s_mode+1)%MODE_COUNT; s_field=0; s_nresult=0; equations_redraw(); return; }
    if (key_is_exe(k) && !s_editing) {
        if      (s_mode==MODE_QUAD)   solve_quad();
        else if (s_mode==MODE_LINEAR) solve_linear();
        else                          solve_single();
        return;
    }
    /* Field navigation */
    if (k == KEY_DOWN && !s_editing) {
        s_field = (s_field+1)%field_count(); equations_redraw(); return;
    }
    if (k == KEY_UP && !s_editing) {
        s_field = (s_field+field_count()-1)%field_count(); equations_redraw(); return;
    }
    if (k == KEY_ALPHA) {
        /* Start editing from the field's current value; ALPHA again cancels */
        s_editing = !s_editing;
        s_shift = false;
        if (s_editing) {
            strncpy(s_input, field_text(s_field), sizeof(s_input) - 1);
            s_input[sizeof(s_input) - 1] = 0;
            s_ilen = (int)strlen(s_input);
        }
        equations_redraw(); return;
    }

    if (s_editing) {
        if (k == KEY_SHIFT) { s_shift = !s_shift; return; }
        if (k == KEY_BACKSPACE) {
            if (s_ilen > 0) s_input[--s_ilen] = 0;
            equations_redraw(); return;
        }
        if (key_is_exe(k)) {
            /* Commit field value */
            char *dst = field_text(s_field);
            strncpy(dst, s_input, (size_t)field_cap());
            dst[field_cap()] = 0;
            s_input[0] = 0; s_ilen = 0; s_editing = false;
            equations_redraw(); return;
        }
        /* Text input */
        const char *ins = expr_key_text(k, s_shift);
        if (ins && s_ilen + (int)strlen(ins) <= field_cap()) {
            strcat(s_input, ins);
            s_ilen = (int)strlen(s_input);
            s_shift = false;
            equations_redraw();
        }
    }
}
