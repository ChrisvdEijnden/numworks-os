
/* ================================================================
 * NumWorks OS — Functions App (Functies)
 * File: apps/functions/functions.c
 *
 * Features:
 *  - Enter up to 4 functions f(x) = ...
 *  - Plot colourful graphs on 320x180 canvas
 *  - Table view: x and y values
 *  - Derivative estimate at a point
 *
 * Graph area: x=0..319, y=44..223 (180 px tall)
 * Coordinate system: mapped [-10..10] x [-6..6] by default.
 * ================================================================ */
#include "functions.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../common/expr.h"
#include "../../include/config.h"
#include "../../include/string.h"
#include "../../include/stdio.h"
#include "../../include/math.h"
#include "../../include/stdlib.h"

#define HEADER_H  28
#define FOOTER_H  18
#define GRAPH_Y0  HEADER_H
#define GRAPH_H   (LCD_HEIGHT - HEADER_H - FOOTER_H)
#define GRAPH_W   LCD_WIDTH

#define MAX_FNS   4
#define FN_LEN    48

typedef enum { VIEW_GRAPH, VIEW_TABLE, VIEW_ENTER } fn_view_t;

static char     s_fn[MAX_FNS][FN_LEN];
static uint16_t s_fn_colour[MAX_FNS];
static int      s_nfn    = 0;
static int      s_sel    = 0;   /* selected function */
static fn_view_t s_view  = VIEW_ENTER;
static float    s_xmin   = -10.0f;
static float    s_xmax   =  10.0f;
static float    s_ymin   = -6.0f;
static float    s_ymax   =  6.0f;
static char     s_entry[FN_LEN];
static int      s_elen   = 0;
static bool     s_shift  = false;
static const char *s_msg = "";   /* last entry error, shown under the list */

static const uint16_t COLOURS[MAX_FNS] = {
    RGB(80,200,255), RGB(255,160,40), RGB(100,255,100), RGB(255,80,200)
};

/* Screen → world coordinates */
static float scr_to_wx(int sx) {
    return s_xmin + (float)sx / GRAPH_W * (s_xmax - s_xmin);
}
static int wy_to_scr(float wy) {
    int y = (int)((s_ymax - wy) / (s_ymax - s_ymin) * GRAPH_H);
    return GRAPH_Y0 + y;
}

/* Evaluate f(x) — returns NaN on error or outside the domain */
static float eval_fn(const char *expr, float x) {
    double y;
    if (expr_eval(expr, (double)x, &y) != EXPR_OK) return NAN;
    return (float)y;
}

static void draw_axes(void) {
    /* x-axis */
    int y0 = wy_to_scr(0.0f);
    if (y0 >= GRAPH_Y0 && y0 < GRAPH_Y0 + GRAPH_H)
        display_hline(0, y0, GRAPH_W, RGB(80,80,80));
    /* y-axis */
    int xs = (int)((0 - s_xmin) / (s_xmax - s_xmin) * GRAPH_W);
    if (xs >= 0 && xs < GRAPH_W)
        display_vline(xs, GRAPH_Y0, GRAPH_H, RGB(80,80,80));
}

static void plot_fn(int fi) {
    float prev_y = NAN;
    int   prev_sy = 0;
    for (int sx = 0; sx < GRAPH_W; sx++) {
        float wx = scr_to_wx(sx);
        float wy = eval_fn(s_fn[fi], wx);
        if (!isnan(wy) && !isinf(wy)) {
            int sy = wy_to_scr(wy);
            if (sy >= GRAPH_Y0 && sy < GRAPH_Y0 + GRAPH_H) {
                if (!isnan(prev_y) && prev_sy >= GRAPH_Y0 && prev_sy < GRAPH_Y0 + GRAPH_H) {
                    /* Draw line segment */
                    int dy = sy - prev_sy;
                    if (dy < 0) dy = -dy;
                    if (dy < 10) {
                        int ay = prev_sy < sy ? prev_sy : sy;
                        int ah = (sy - prev_sy < 0 ? prev_sy - sy : sy - prev_sy) + 1;
                        for (int yy = ay; yy < ay+ah; yy++)
                            display_pixel(sx, yy, s_fn_colour[fi]);
                    }
                }
                display_pixel(sx, sy, s_fn_colour[fi]);
            }
            prev_y  = wy;
            prev_sy = sy;
        } else {
            prev_y = NAN;
        }
    }
}

static void draw_table(void) {
    display_fill(RGB(10,10,20));
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, RGB(30,80,200));
    display_str(6, 8, "Tabel  f(x)", WHITE, RGB(30,80,200));
    if (s_nfn == 0) { display_str(10, 50, "Geen functies", WHITE, RGB(10,10,20)); return; }
    int row_h = 14;
    /* Header */
    display_str(2, HEADER_H+2, "  x       f(x)", YELLOW, RGB(10,10,20));
    for (int r = 0; r < 12; r++) {
        float x = s_xmin + r * (s_xmax - s_xmin) / 12.0f;
        float y = eval_fn(s_fn[s_sel], x);
        char line[48];
        snprintf(line, sizeof(line), "%7.3f  %10.4f", (double)x, (double)y);
        display_str(2, HEADER_H + 16 + r*row_h, line, RGB(200,230,255), RGB(10,10,20));
    }
}

static void draw_enter(void) {
    display_fill(RGB(10,10,20));
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, RGB(30,80,200));
    display_str(6, 8, "Functies invoeren", WHITE, RGB(30,80,200));
    for (int i = 0; i < MAX_FNS; i++) {
        int y = HEADER_H + 8 + i*30;
        bool active = (i == s_nfn && s_view == VIEW_ENTER);
        uint16_t bg = active ? RGB(40,40,70) : RGB(20,20,35);
        display_fill_rect(0, y, LCD_WIDTH, 24, bg);
        char label[256];
        snprintf(label, sizeof(label), "f%d(x)= %s%s",
                 i+1, i < s_nfn ? s_fn[i] : (active ? s_entry : ""),
                 active ? "_" : "");
        display_str(4, y+6, label, COLOURS[i], bg);
    }
    display_str(4, HEADER_H+140, "OK:Opslaan  TOOLBOX:Grafiek",
                YELLOW, RGB(10,10,20));
    display_str(4, HEADER_H+154, "VAR:Tabel  HOME:Terug",
                YELLOW, RGB(10,10,20));
    display_str(4, HEADER_H+168, s_shift ? "SHIFT actief: 9=(  0=)  sin=asin"
                                         : "SHIFT 9: (   SHIFT 0: )   XNT: x",
                s_shift ? CYAN : GREY, RGB(10,10,20));
    if (s_msg[0])
        display_str(4, HEADER_H+184, s_msg, RED, RGB(10,10,20));
}

void functions_redraw(void) {
    if (s_view == VIEW_ENTER) { draw_enter(); return; }
    if (s_view == VIEW_TABLE) { draw_table(); return; }

    /* Graph view */
    display_fill_rect(0, GRAPH_Y0, GRAPH_W, GRAPH_H, RGB(5,5,15));
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, RGB(30,80,200));
    display_str(6, 8, "Functies grafiek", WHITE, RGB(30,80,200));
    display_fill_rect(0, LCD_HEIGHT-FOOTER_H, LCD_WIDTH, FOOTER_H, RGB(25,25,40));
    display_str(4, LCD_HEIGHT-FOOTER_H+4, "HOME:Terug  VAR:Tabel  ALPHA:Invoer",
                YELLOW, RGB(25,25,40));
    draw_axes();
    for (int i = 0; i < s_nfn; i++) plot_fn(i);
}

void functions_init(void) {
    s_nfn = 0; s_sel = 0; s_view = VIEW_ENTER; s_elen = 0;
    for (int i = 0; i < MAX_FNS; i++) {
        s_fn_colour[i] = COLOURS[i];
        s_fn[i][0] = 0;
    }
}

void functions_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) {
        if (s_view != VIEW_ENTER) { s_view = VIEW_ENTER; functions_redraw(); }
        else kernel_set_app(APP_HOME);
        return;
    }
    if (k == KEY_TOOLBOX) { s_view = VIEW_GRAPH; functions_redraw(); return; }
    if (k == KEY_VAR)     { s_view = VIEW_TABLE; s_sel = 0; functions_redraw(); return; }
    if (k == KEY_ALPHA)   { s_view = VIEW_ENTER; functions_redraw(); return; }

    if (s_view == VIEW_ENTER) {
        s_msg = "";
        if (k == KEY_SHIFT) {
            s_shift = !s_shift;
        } else if (k == KEY_BACKSPACE && s_elen > 0) {
            s_entry[--s_elen] = 0;
        } else if (key_is_exe(k)) {
            if (s_elen > 0 && s_nfn < MAX_FNS) {
                /* Only store functions that parse; out-of-domain values
                 * (e.g. ln(x) at x=0) are fine and just leave gaps. */
                double y;
                expr_status_t st = expr_eval(s_entry, 0.0, &y);
                if (st != EXPR_OK) {
                    s_msg = expr_error(st);
                } else {
                    strncpy(s_fn[s_nfn], s_entry, FN_LEN-1);
                    s_nfn++;
                    s_elen = 0; s_entry[0] = 0;
                }
            } else if (s_nfn >= MAX_FNS) {
                s_msg = "maximaal 4 functies";
            }
        } else {
            /* Append typed text */
            const char *ins = expr_key_text(k, s_shift);
            if (ins && s_elen + (int)strlen(ins) < FN_LEN-1) {
                strcat(s_entry, ins);
                s_elen = (int)strlen(s_entry);
                s_shift = false;
            }
        }
        draw_enter();
    } else if (s_view == VIEW_TABLE) {
        if (k == KEY_UP && s_sel > 0) { s_sel--; draw_table(); }
        else if (k == KEY_DOWN && s_sel < s_nfn-1) { s_sel++; draw_table(); }
    } else if (s_view == VIEW_GRAPH) {
        float dx = (s_xmax - s_xmin) * 0.2f;
        float dy = (s_ymax - s_ymin) * 0.2f;
        if (k == KEY_LEFT)  { s_xmin -= dx; s_xmax -= dx; functions_redraw(); }
        else if (k == KEY_RIGHT) { s_xmin += dx; s_xmax += dx; functions_redraw(); }
        else if (k == KEY_UP)    { s_ymin += dy; s_ymax += dy; functions_redraw(); }
        else if (k == KEY_DOWN)  { s_ymin -= dy; s_ymax -= dy; functions_redraw(); }
        else if (k == KEY_PLUS || k == KEY_MINUS) {
            /* Zoom about the centre; half-ranges come from the old bounds */
            float f  = (k == KEY_PLUS) ? 0.4f : 0.6f;
            float cx = (s_xmin+s_xmax)/2, cy = (s_ymin+s_ymax)/2;
            float hw = (s_xmax-s_xmin)*f,  hh = (s_ymax-s_ymin)*f;
            s_xmin = cx-hw; s_xmax = cx+hw;
            s_ymin = cy-hh; s_ymax = cy+hh;
            functions_redraw();
        }
    }
}
