/* ================================================================
 * NumWorks OS — Functions App (Functies)
 * File: apps/functions/functions.c
 *
 *  - Enter up to 4 functions f(x) = ...
 *  - Graph: arrows pan, + / - zoom
 *      OK       trace: a cursor on the selected function, moved with
 *               LEFT/RIGHT (UP/DOWN picks another function); the
 *               window follows it
 *      TOOLBOX  analysis: the next zero, minimum, maximum or
 *               intersection to the right of the cursor, within the
 *               visible window (apps/common/analysis.c)
 *  - Table: x and f(x)
 *
 * Graph area: x=0..319, below the title and tab bars, a banner under it.
 * Coordinate system: [-10..10] x [-6..6] by default.
 * ================================================================ */
#include "functions.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../common/expr.h"
#include "../common/analysis.h"
#include "../../include/config.h"
#include "../../ui/lang.h"
#include "../../ui/theme.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define HEADER_H  (UI_TITLE_H + UI_TAB_H)
#define FOOTER_H  20
#define GRAPH_Y0  HEADER_H
#define GRAPH_H   (LCD_HEIGHT - HEADER_H - FOOTER_H)
#define GRAPH_W   LCD_WIDTH
#define TRACE_PX  4             /* cursor step, in pixels */

#define MAX_FNS   4
#define FN_LEN    48


typedef enum { VIEW_GRAPH, VIEW_TABLE, VIEW_ENTER } fn_view_t;
typedef enum { AN_ZERO, AN_MIN, AN_MAX, AN_CROSS, AN_COUNT } an_kind_t;

static char     s_fn[MAX_FNS][FN_LEN];
static int      s_nfn    = 0;
static int      s_sel    = 0;   /* selected function (table, trace) */
static fn_view_t s_view  = VIEW_ENTER;
static double   s_xmin   = -10.0, s_xmax = 10.0;
static double   s_ymin   = -6.0,  s_ymax = 6.0;
static char     s_entry[FN_LEN];
static int      s_elen   = 0;
static bool     s_shift  = false;
static int      s_row    = 0;   /* row being edited; s_nfn = the empty "new" row */
static const char *s_msg = "";  /* last entry error, shown under the list */

static bool     s_trace  = false;
static double   s_tx     = 0.0; /* trace cursor */
static bool     s_menu   = false;
static int      s_menu_sel = 0;
static char     s_info[64];     /* footer text: cursor or analysis result */

static const uint16_t COLOURS[MAX_FNS] = { T_RED, T_BLUE, T_GREEN, T_YELLOW };

/* ── Coordinates and evaluation ───────────────────────────────── */
static double scr_to_wx(int sx) { return s_xmin + (double)sx / GRAPH_W * (s_xmax - s_xmin); }
static int wx_to_scr(double wx) { return (int)lround((wx - s_xmin) / (s_xmax - s_xmin) * GRAPH_W); }
static int wy_to_scr(double wy) {
    double y = (s_ymax - wy) / (s_ymax - s_ymin) * GRAPH_H;
    if (y < -10000) y = -10000;
    if (y > 10000) y = 10000;
    return GRAPH_Y0 + (int)lround(y);
}

/* f(x), NaN on error or outside the domain */
static double eval_fn(int fi, double x) {
    double y;
    if (expr_eval(s_fn[fi], x, &y) != EXPR_OK) return NAN;
    return y;
}
static bool finite_d(double v) { return !isnan(v) && !isinf(v); }

/* ── Drawing ───────────────────────────────────────────────────── */
/* A light grid (a line per unit, or per 10 when that's too dense),
 * then the axes */
static void grid_lines(double lo, double hi, int pixels, bool vertical) {
    double step = 1.0;
    while ((hi - lo) / step > pixels / 8.0) step *= 10.0;
    for (double v = ceil(lo / step) * step; v <= hi; v += step) {
        if (vertical) { int sx = wx_to_scr(v); if (sx >= 0 && sx < GRAPH_W) display_vline(sx, GRAPH_Y0, GRAPH_H, T_WALL_DARK); }
        else { int sy = wy_to_scr(v); if (sy >= GRAPH_Y0 && sy < GRAPH_Y0 + GRAPH_H) display_hline(0, sy, GRAPH_W, T_WALL_DARK); }
    }
}

static void draw_axes(void) {
    grid_lines(s_xmin, s_xmax, GRAPH_W, true);
    grid_lines(s_ymin, s_ymax, GRAPH_H, false);
    int y0 = wy_to_scr(0.0);
    if (y0 >= GRAPH_Y0 && y0 < GRAPH_Y0 + GRAPH_H) display_hline(0, y0, GRAPH_W, T_GRAY_DARKEST);
    int xs = wx_to_scr(0.0);
    if (xs >= 0 && xs < GRAPH_W) display_vline(xs, GRAPH_Y0, GRAPH_H, T_GRAY_DARKEST);
}

static bool on_graph(int sy) { return sy >= GRAPH_Y0 && sy < GRAPH_Y0 + GRAPH_H; }

static void plot_fn(int fi) {
    bool have_prev = false;
    int prev_sy = 0;
    for (int sx = 0; sx < GRAPH_W; sx++) {
        double wy = eval_fn(fi, scr_to_wx(sx));
        if (!finite_d(wy)) { have_prev = false; continue; }
        int sy = wy_to_scr(wy);
        /* Join to the previous point unless it's a jump (a pole) */
        if (have_prev && abs(sy - prev_sy) < GRAPH_H / 2 && (on_graph(sy) || on_graph(prev_sy))) {
            int a = prev_sy < sy ? prev_sy : sy, b = prev_sy < sy ? sy : prev_sy;
            if (a < GRAPH_Y0) a = GRAPH_Y0;
            if (b > GRAPH_Y0 + GRAPH_H - 1) b = GRAPH_Y0 + GRAPH_H - 1;
            for (int yy = a; yy <= b; yy++) display_fill_rect(sx, yy, 2, 2, COLOURS[fi]);
        } else if (on_graph(sy)) {
            display_fill_rect(sx, sy, 2, 2, COLOURS[fi]);
        }
        have_prev = true;
        prev_sy = sy;
    }
}

/* Shown to 6 digits. A value that is tiny next to the window is shown
 * as 0: an extremum found by comparing values is only good to about
 * 1e-8, so x=1e-08 would really mean x=0. */
static void fmt_num(char *buf, int n, double v, double scale) {
    if (fabs(v) < 1e-7 * scale) v = 0.0;
    snprintf(buf, (size_t)n, "%.6g", v);
}

static void draw_cursor(void) {
    double y = eval_fn(s_sel, s_tx);
    int sx = wx_to_scr(s_tx);
    if (sx >= 0 && sx < GRAPH_W) display_vline(sx, GRAPH_Y0, GRAPH_H, T_GRAY_DARK);
    if (finite_d(y)) {
        int sy = wy_to_scr(y);
        if (on_graph(sy)) {
            display_fill_rect(sx - 4, sy - 4, 9, 9, T_TEXT);
            display_fill_rect(sx - 3, sy - 3, 7, 7, COLOURS[s_sel]);
        }
    }
}

static void trace_info(void) {
    char xs[20], ys[20];
    double y = eval_fn(s_sel, s_tx);
    fmt_num(xs, sizeof xs, s_tx, s_xmax - s_xmin);
    if (finite_d(y)) fmt_num(ys, sizeof ys, y, s_ymax - s_ymin); else snprintf(ys, sizeof ys, "-");
    snprintf(s_info, sizeof s_info, "f%d  x=%s  y=%s", s_sel + 1, xs, ys);
}

static const char *an_name(int k) {
    switch (k) {
    case AN_ZERO: return TR("Nulpunt", "Zero");
    case AN_MIN:  return TR("Minimum", "Minimum");
    case AN_MAX:  return TR("Maximum", "Maximum");
    default:      return TR("Snijpunt", "Intersection");
    }
}

/* A pop-up over the graph: a dark title strip, then the choices */
static void draw_menu(void) {
    int16_t x = 70, y = GRAPH_Y0 + 18, w = 180, h = 22 + AN_COUNT * 26;
    display_fill_rect(x, y, w, h, WHITE);
    display_rect(x - 1, y - 1, w + 2, h + 2, T_GRAY_DARK);
    display_fill_rect(x, y, w, 22, RGB(0x69, 0x64, 0x75));
    ui_text_center(x + w / 2, y + 4, TR("Analyse", "Analysis"), &font_small, WHITE, RGB(0x69, 0x64, 0x75));
    for (int i = 0; i < AN_COUNT; i++) {
        uint16_t bg = i == s_menu_sel ? T_SELECT : WHITE;
        display_fill_rect(x, (int16_t)(y + 22 + i * 26), w, 26, bg);
        display_hline(x, (int16_t)(y + 22 + i * 26), w, T_GRAY_BRIGHT);
        display_text((int16_t)(x + 10), (int16_t)(y + 22 + i * 26 + 6), an_name(i), &font_small, T_TEXT, bg);
    }
}

static void draw_footer(void) {
    display_fill_rect(0, LCD_HEIGHT - FOOTER_H, LCD_WIDTH, FOOTER_H, T_GRAY_BRIGHT);
    display_hline(0, LCD_HEIGHT - FOOTER_H, LCD_WIDTH, T_GRAY_MIDDLE);
    const char *t = s_info[0] ? s_info
        : s_trace ? TR("L/R:volg  U/D:functie  TOOLBOX:analyse", "L/R:trace  U/D:function  TOOLBOX:analysis")
                  : TR("OK:volg  TOOLBOX:analyse  +/-:zoom", "OK:trace  TOOLBOX:analysis  +/-:zoom");
    display_text(6, LCD_HEIGHT - FOOTER_H + 4, t, &font_small, s_info[0] ? T_TEXT : T_GRAY_VDARK, T_GRAY_BRIGHT);
}

/* Title and tabs: which view is shown */
static void draw_header(int tab) {
    static const char *nl[3] = { "Expressies", "Grafiek", "Tabel" };
    static const char *en[3] = { "Expressions", "Graph", "Table" };
    const char *names[3];
    for (int i = 0; i < 3; i++) names[i] = TR(nl[i], en[i]);
    ui_title_bar_mods(TR("Functies", "Functions"), s_shift, false);
    ui_tabs(UI_TITLE_H, names, 3, tab, false);
}

static void draw_graph(void) {
    draw_header(1);
    display_fill_rect(0, GRAPH_Y0, GRAPH_W, GRAPH_H, WHITE);
    draw_axes();
    for (int i = 0; i < s_nfn; i++) plot_fn(i);
    if (s_trace && s_nfn > 0) draw_cursor();
    draw_footer();
    if (s_menu) draw_menu();
}

/* x and f(x) in two columns, rows alternately white and light grey */
static void draw_table(void) {
    draw_header(2);
    display_fill_rect(0, UI_TITLE_H + UI_TAB_H, LCD_WIDTH, LCD_HEIGHT - UI_TITLE_H - UI_TAB_H, WHITE);
    if (s_nfn == 0) {
        ui_text_center(LCD_WIDTH / 2, 120, TR("Geen functies", "No functions"), &font_large, T_GRAY_VDARK, WHITE);
        return;
    }
    int16_t y0 = UI_TITLE_H + UI_TAB_H, cw = LCD_WIDTH / 2, rh = 16;
    char head[16];
    snprintf(head, sizeof head, "f%d(x)", s_sel + 1);
    display_fill_rect(0, y0, LCD_WIDTH, 22, T_GRAY_WHITE);
    display_fill_rect(cw, y0, cw, 2, COLOURS[s_sel]);
    ui_text_center(cw / 2, (int16_t)(y0 + 4), "x", &font_small, T_TEXT, T_GRAY_WHITE);
    ui_text_center((int16_t)(cw + cw / 2), (int16_t)(y0 + 4), head, &font_small, T_TEXT, T_GRAY_WHITE);
    for (int r = 0; r < 10; r++) {
        double x = s_xmin + r * (s_xmax - s_xmin) / 10.0;
        double y = eval_fn(s_sel, x);
        int16_t ry = (int16_t)(y0 + 22 + r * rh);
        uint16_t bg = r % 2 ? T_WALL : WHITE;
        display_fill_rect(0, ry, LCD_WIDTH, rh, bg);
        char a[24], b[24];
        snprintf(a, sizeof a, "%.4g", x);
        if (finite_d(y)) snprintf(b, sizeof b, "%.6g", y); else snprintf(b, sizeof b, TR("ongedef.", "undef"));
        ui_text_right((int16_t)(cw - 10), (int16_t)(ry + 1), a, &font_small, T_TEXT, bg);
        ui_text_right((int16_t)(LCD_WIDTH - 10), (int16_t)(ry + 1), b, &font_small, T_TEXT, bg);
    }
    display_vline(cw, y0, (int16_t)(22 + 10 * rh), T_GRAY_MIDDLE);
}

/* Up to four functions, each with its colour on the left */
static void draw_enter(void) {
    draw_header(0);
    display_fill_rect(0, UI_TITLE_H + UI_TAB_H, LCD_WIDTH, LCD_HEIGHT - UI_TITLE_H - UI_TAB_H, T_WALL);
    for (int i = 0; i < MAX_FNS; i++) {
        int16_t y = (int16_t)(UI_TITLE_H + UI_TAB_H + 8 + i * 34);
        bool active = (i == s_row && s_view == VIEW_ENTER);
        uint16_t bg = active ? T_SELECT : WHITE;
        display_fill_rect(0, y, LCD_WIDTH, 34, bg);
        display_hline(0, (int16_t)(y + 33), LCD_WIDTH, T_GRAY_BRIGHT);
        display_fill_rect(0, y, 3, 34, COLOURS[i]);
        char label[64];
        const char *body = active ? s_entry : (i < s_nfn ? s_fn[i] : "");
        snprintf(label, sizeof(label), "f%d(x)=%.47s", i + 1, body);
        display_text(12, (int16_t)(y + 8), label, &font_large, i < s_nfn || active ? T_TEXT : T_GRAY_DARK, bg);
        if (active) display_fill_rect((int16_t)(12 + (int)strlen(label) * 10), (int16_t)(y + 7), 1, 20, T_TEXT);
    }
    int16_t hy = (int16_t)(UI_TITLE_H + UI_TAB_H + 8 + MAX_FNS * 34 + 6);
    display_text(8, hy, TR("TOOLBOX: grafiek  VAR: tabel  leeg+OK: wis", "TOOLBOX: graph  VAR: table  empty+OK: delete"),
                 &font_small, T_GRAY_VDARK, T_WALL);
    if (s_msg[0]) display_text(8, (int16_t)(hy + 16), s_msg, &font_small, T_RED, T_WALL);
}

void functions_redraw(void) {
    if (s_view == VIEW_ENTER) draw_enter();
    else if (s_view == VIEW_TABLE) draw_table();
    else draw_graph();
}

/* ── Trace and analysis ────────────────────────────────────────── */
/* Keep the cursor inside the window: shift it by a fifth when needed */
static void follow_cursor(void) {
    double w = s_xmax - s_xmin;
    while (s_tx > s_xmax) { s_xmin += w / 5; s_xmax += w / 5; }
    while (s_tx < s_xmin) { s_xmin -= w / 5; s_xmax -= w / 5; }
    double y = eval_fn(s_sel, s_tx), h = s_ymax - s_ymin;
    if (finite_d(y) && (y > s_ymax || y < s_ymin)) { s_ymin = y - h / 2; s_ymax = y + h / 2; }
}

typedef struct { int f, g; } pair_t;
static double an_f(void *ctx, double x) { return eval_fn(*(int *)ctx, x); }
static double an_diff(void *ctx, double x) {
    const pair_t *p = (const pair_t *)ctx;
    return eval_fn(p->f, x) - eval_fn(p->g, x);
}

/* Search right of the cursor (or of the window's left edge) up to the
 * window's right edge. Exposed for tests. */
bool functions_analyse(int kind, double *x_out, int *other_fn) {
    if (s_nfn == 0) return false;
    double from = s_trace ? s_tx : s_xmin, to = s_xmax;
    double step = (s_xmax - s_xmin) / GRAPH_W * 0.5;   /* skip the point under the cursor */
    from += step;
    int f = s_sel;
    switch (kind) {
    case AN_ZERO: return an_next_zero(an_f, &f, from, to, x_out);
    case AN_MIN:  return an_next_extremum(an_f, &f, from, to, false, x_out);
    case AN_MAX:  return an_next_extremum(an_f, &f, from, to, true, x_out);
    default: {
        bool found = false;
        for (int g = 0; g < s_nfn; g++) {
            if (g == f) continue;
            pair_t p = { f, g };
            double x;
            if (an_next_zero(an_diff, &p, from, to, &x) && (!found || x < *x_out)) {
                *x_out = x; found = true;
                if (other_fn) *other_fn = g;
            }
        }
        return found;
    }
    }
}

static void run_analysis(int kind) {
    double x;
    int g = -1;
    if (kind == AN_CROSS && s_nfn < 2) {
        snprintf(s_info, sizeof s_info, "%s", TR("Snijpunt: maak eerst 2 functies", "Intersection: enter 2 functions"));
        return;
    }
    if (!functions_analyse(kind, &x, &g)) {
        snprintf(s_info, sizeof s_info, TR("%.16s: geen gevonden (rechts)", "%.16s: none found (right)"), an_name(kind));
        return;
    }
    s_trace = true;
    s_tx = x;
    char xs[20], ys[20];
    fmt_num(xs, sizeof xs, x, s_xmax - s_xmin);
    fmt_num(ys, sizeof ys, kind == AN_ZERO ? 0.0 : eval_fn(s_sel, x), s_ymax - s_ymin);
    if (kind == AN_CROSS) snprintf(s_info, sizeof s_info, TR("Snijpunt f%d: x=%s y=%s", "Meets f%d: x=%s y=%s"), g + 1, xs, ys);
    else snprintf(s_info, sizeof s_info, "%.16s: x=%s y=%s", an_name(kind), xs, ys);
    follow_cursor();
}

/* ── Entry ─────────────────────────────────────────────────────── */
/* Put row r's function (or nothing, for the new row) in the entry line */
static void select_row(int r) {
    int last = s_nfn < MAX_FNS ? s_nfn : MAX_FNS - 1;
    s_row = r < 0 ? 0 : (r > last ? last : r);
    if (s_row < s_nfn) strncpy(s_entry, s_fn[s_row], FN_LEN - 1);
    else               s_entry[0] = 0;
    s_entry[FN_LEN - 1] = 0;
    s_elen = (int)strlen(s_entry);
}

void functions_init(void) {
    s_nfn = 0; s_sel = 0; s_view = VIEW_ENTER; s_elen = 0; s_row = 0;
    s_entry[0] = 0;
    for (int i = 0; i < MAX_FNS; i++) s_fn[i][0] = 0;
    s_trace = false; s_menu = false; s_info[0] = 0;
    s_xmin = -10.0; s_xmax = 10.0; s_ymin = -6.0; s_ymax = 6.0;
}

static void graph_key(key_code_t k) {
    if (s_menu) {
        if (k == KEY_UP && s_menu_sel > 0) s_menu_sel--;
        else if (k == KEY_DOWN && s_menu_sel < AN_COUNT - 1) s_menu_sel++;
        else if (key_is_exe(k)) { s_menu = false; run_analysis(s_menu_sel); }
        else if (k == KEY_BACK || k == KEY_TOOLBOX) s_menu = false;
        draw_graph();
        return;
    }
    s_info[0] = 0;
    double w = s_xmax - s_xmin, h = s_ymax - s_ymin;
    if (k == KEY_TOOLBOX) {
        if (s_nfn > 0) { s_menu = true; s_menu_sel = 0; }
    } else if (key_is_exe(k)) {
        if (s_nfn > 0) {
            s_trace = !s_trace;
            if (s_trace) { s_tx = (s_xmin + s_xmax) / 2; follow_cursor(); }
        }
    } else if (s_trace) {
        if (k == KEY_LEFT || k == KEY_RIGHT) {
            s_tx += (k == KEY_RIGHT ? 1 : -1) * w / GRAPH_W * TRACE_PX;
            follow_cursor();
        } else if (k == KEY_UP || k == KEY_DOWN) {
            s_sel = (s_sel + (k == KEY_DOWN ? 1 : s_nfn - 1)) % s_nfn;
            follow_cursor();
        } else if (k != KEY_PLUS && k != KEY_MINUS) {
            return;
        }
    } else if (k == KEY_LEFT)  { s_xmin -= w / 5; s_xmax -= w / 5; }
    else if (k == KEY_RIGHT)   { s_xmin += w / 5; s_xmax += w / 5; }
    else if (k == KEY_UP)      { s_ymin += h / 5; s_ymax += h / 5; }
    else if (k == KEY_DOWN)    { s_ymin -= h / 5; s_ymax -= h / 5; }
    else if (k != KEY_PLUS && k != KEY_MINUS) return;
    if (k == KEY_PLUS || k == KEY_MINUS) {
        /* Zoom about the cursor (or the centre) */
        double f  = (k == KEY_PLUS) ? 0.4 : 0.6;
        double cx = s_trace ? s_tx : (s_xmin + s_xmax) / 2, cy = (s_ymin + s_ymax) / 2;
        s_xmin = cx - w * f; s_xmax = cx + w * f;
        s_ymin = cy - h * f; s_ymax = cy + h * f;
    }
    if (s_trace) trace_info();
    draw_graph();
}

void functions_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || (k == KEY_BACK && !s_menu)) {
        if (s_view == VIEW_GRAPH && s_trace && k == KEY_BACK) { s_trace = false; s_info[0] = 0; draw_graph(); }
        else if (s_view != VIEW_ENTER) { s_view = VIEW_ENTER; functions_redraw(); }
        else kernel_set_app(APP_HOME);
        return;
    }
    if (s_view == VIEW_GRAPH) {
        if (k == KEY_VAR)   { s_view = VIEW_TABLE; s_menu = false; functions_redraw(); return; }
        if (k == KEY_ALPHA) { s_view = VIEW_ENTER; s_menu = false; functions_redraw(); return; }
        graph_key(k);
        return;
    }
    if (k == KEY_TOOLBOX) { s_view = VIEW_GRAPH; s_info[0] = 0; if (s_sel >= s_nfn) s_sel = 0; functions_redraw(); return; }
    if (k == KEY_VAR)     { s_view = VIEW_TABLE; if (s_sel >= s_nfn) s_sel = 0; functions_redraw(); return; }
    if (k == KEY_ALPHA)   { s_view = VIEW_ENTER; functions_redraw(); return; }

    if (s_view == VIEW_ENTER) {
        s_msg = "";
        if (k == KEY_SHIFT) {
            s_shift = !s_shift;
        } else if (k == KEY_BACKSPACE && s_elen > 0) {
            s_entry[--s_elen] = 0;
        } else if (k == KEY_UP) {
            select_row(s_row - 1);
        } else if (k == KEY_DOWN) {
            select_row(s_row + 1);
        } else if (key_is_exe(k)) {
            if (s_elen == 0) {
                /* Emptied an existing function: delete it */
                if (s_row < s_nfn) {
                    for (int i = s_row; i < s_nfn - 1; i++) memcpy(s_fn[i], s_fn[i + 1], FN_LEN);
                    s_nfn--;
                    s_fn[s_nfn][0] = 0;
                    if (s_sel >= s_nfn) s_sel = 0;
                    select_row(s_row);
                }
            } else {
                /* Only store functions that parse; out-of-domain values
                 * (e.g. ln(x) at x=0) are fine and just leave gaps. */
                double y;
                expr_status_t st = expr_eval(s_entry, 0.0, &y);
                if (st != EXPR_OK) {
                    s_msg = expr_error(st);
                } else {
                    strncpy(s_fn[s_row], s_entry, FN_LEN - 1);
                    s_fn[s_row][FN_LEN - 1] = 0;
                    if (s_row == s_nfn) s_nfn++;   /* filled the new row */
                    select_row(s_nfn);             /* on to the next empty row */
                }
            }
        } else {
            const char *ins = expr_key_text(k, s_shift);
            if (ins && s_elen + (int)strlen(ins) < FN_LEN - 1) {
                strcat(s_entry, ins);
                s_elen = (int)strlen(s_entry);
                s_shift = false;
            }
        }
        draw_enter();
    } else if (s_view == VIEW_TABLE) {
        if (k == KEY_UP && s_sel > 0) { s_sel--; draw_table(); }
        else if (k == KEY_DOWN && s_sel < s_nfn - 1) { s_sel++; draw_table(); }
    }
}

/* For tests */
const char *functions_info(void) { return s_info; }
bool functions_tracing(void) { return s_trace; }
double functions_cursor_x(void) { return s_tx; }
