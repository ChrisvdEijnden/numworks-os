/* ================================================================
 * NumWorks OS — Statistics App (Statistiek)
 * File: apps/statistics/statistics.c
 *
 * Three tabs (UP from the top row reaches the tab bar, LEFT/RIGHT
 * there switches; on the Stats and Plot tabs LEFT/RIGHT switch too):
 *   Data   a table of X and Y values. Typing starts editing a cell;
 *          OK stores it (expressions like 1/3 or 2^5 are fine) and
 *          goes down. BACKSPACE clears a cell; a row with both cells
 *          empty is removed.
 *   Stats  n, sum, mean, median, quartiles, min, max, range, standard
 *          deviations of X (apps/common/stats.c); with X/Y pairs also
 *          the least-squares line y = ax + b and r.
 *   Plot   with pairs: scatter plot and the regression line;
 *          otherwise a box plot and a histogram of X.
 * The data is kept in STATS_FILE (one "x,y" per line), so it survives
 * a restart and can be prepared on the PC and uploaded.
 * ================================================================ */
#include "statistics.h"
#include "../common/stats.h"
#include "../common/expr.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../fs/flashfs.h"
#include "../../include/config.h"
#include "../../ui/lang.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C_BG    RGB(10,10,20)
#define C_HDR   RGB(30,80,200)
#define C_TAB   RGB(20,50,140)
#define C_CELL  RGB(20,20,35)
#define C_SEL   RGB(60,60,120)
#define C_TEXT  RGB(220,230,255)

#define HEADER_H 24
#define ROW_H    16
#define TABLE_Y  (HEADER_H + 18)
#define VIS_ROWS ((LCD_HEIGHT - TABLE_Y - 14) / ROW_H)
#define COL_X    40
#define COL_Y    180
#define COL_W    136

enum { TAB_DATA, TAB_STATS, TAB_PLOT, TAB_COUNT };

static double s_x[STATS_MAX], s_y[STATS_MAX];   /* NAN: empty cell */
static int    s_rows;                           /* rows in use */
static int    s_tab;
static bool   s_in_tabs;                        /* focus on the tab bar */
static int    s_row, s_col, s_top;              /* data cursor, first row shown */
static char   s_buf[24];                        /* cell being edited */
static int    s_blen;
static bool   s_editing, s_dirty, s_shift;
static const char *s_msg = "";

/* ── Data ──────────────────────────────────────────────────────── */
static double *cell(int row, int col) { return col == 0 ? &s_x[row] : &s_y[row]; }

/* X values (and complete X/Y pairs) for the calculations */
static int collect_x(double *out) {
    int n = 0;
    for (int i = 0; i < s_rows; i++) if (!isnan(s_x[i])) out[n++] = s_x[i];
    return n;
}
static int collect_pairs(double *xs, double *ys) {
    int n = 0;
    for (int i = 0; i < s_rows; i++)
        if (!isnan(s_x[i]) && !isnan(s_y[i])) { xs[n] = s_x[i]; ys[n] = s_y[i]; n++; }
    return n;
}

static void remove_row(int r) {
    for (int i = r; i < s_rows - 1; i++) { s_x[i] = s_x[i + 1]; s_y[i] = s_y[i + 1]; }
    s_rows--;
    s_x[s_rows] = s_y[s_rows] = NAN;
}

void statistics_set_text(const char *text, unsigned len) {
    s_rows = 0;
    for (int i = 0; i < STATS_MAX; i++) s_x[i] = s_y[i] = NAN;
    unsigned i = 0;
    while (i < len && s_rows < STATS_MAX) {
        char line[64];
        unsigned n = 0;
        while (i < len && text[i] != '\n') { if (n < sizeof line - 1) line[n++] = text[i]; i++; }
        i++;
        line[n] = 0;
        char *comma = strchr(line, ',');
        if (comma) *comma = 0;
        char *end;
        double x = strtod(line, &end), y = NAN;
        bool has_x = end != line;
        if (comma) { y = strtod(comma + 1, &end); if (end == comma + 1) y = NAN; }
        if (!has_x) x = NAN;
        if (!has_x && isnan(y)) continue;          /* blank line */
        s_x[s_rows] = x; s_y[s_rows] = y; s_rows++;
    }
}

static void load(void) {
    const char *data;
    uint32_t size;
    if (flashfs_map(STATS_FILE, &data, &size)) statistics_set_text(data, size);
    else statistics_set_text("", 0);
}

static int format_row(int i, char *line, int max) {
    char xs[24] = "", ys[24] = "";
    if (!isnan(s_x[i])) snprintf(xs, sizeof xs, "%.10g", s_x[i]);
    if (!isnan(s_y[i])) snprintf(ys, sizeof ys, "%.10g", s_y[i]);
    return snprintf(line, (size_t)max, "%s,%s\n", xs, ys);
}

/* Write the table back if it changed. Streamed a row at a time, so no
 * buffer for the whole file is needed. */
static void save(void) {
    if (!s_dirty) return;
    char line[56];
    uint32_t total = 0;
    for (int i = 0; i < s_rows; i++) total += (uint32_t)format_row(i, line, sizeof line);
    if (total == 0) { flashfs_delete(STATS_FILE); s_dirty = false; return; }
    if (flashfs_stream_begin(STATS_FILE, total) != 0) return;   /* stays dirty */
    for (int i = 0; i < s_rows; i++) {
        int n = format_row(i, line, sizeof line);
        if (flashfs_stream_write(line, (uint32_t)n) != n) { flashfs_stream_abort(); return; }
    }
    if (flashfs_stream_end() == (int)total) s_dirty = false;
}

/* ── Drawing ───────────────────────────────────────────────────── */
static const char *tab_name(int t) {
    switch (t) {
    case TAB_DATA:  return TR("Gegevens", "Data");
    case TAB_STATS: return TR("Statistiek", "Stats");
    default:        return TR("Grafiek", "Plot");
    }
}

static void draw_header(void) {
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    for (int t = 0; t < TAB_COUNT; t++) {
        int x = 4 + t * 104;
        uint16_t bg = t == s_tab ? (s_in_tabs ? RGB(90,150,255) : C_TAB) : C_HDR;
        display_fill_rect(x, 3, 100, HEADER_H - 6, bg);
        display_str(x + 6, 8, tab_name(t), WHITE, bg);
    }
}

static void fmt(char *b, int n, double v) {
    if (isnan(v)) snprintf(b, (size_t)n, "-");
    else snprintf(b, (size_t)n, "%.8g", fabs(v) < 1e-12 ? 0.0 : v);
}

static void footer(const char *t, uint16_t c) {
    display_fill_rect(0, LCD_HEIGHT - 13, LCD_WIDTH, 13, C_BG);
    display_str(4, LCD_HEIGHT - 11, t, c, C_BG);
}

static void draw_data(void) {
    display_fill_rect(0, HEADER_H, LCD_WIDTH, LCD_HEIGHT - HEADER_H, C_BG);
    display_str(4, HEADER_H + 4, "#", GREY, C_BG);
    display_str(COL_X + 4, HEADER_H + 4, "X", YELLOW, C_BG);
    display_str(COL_Y + 4, HEADER_H + 4, "Y", YELLOW, C_BG);
    for (int v = 0; v < VIS_ROWS; v++) {
        int r = s_top + v, y = TABLE_Y + v * ROW_H;
        if (r > s_rows || r >= STATS_MAX) break;     /* row s_rows: the empty new one */
        char num[12];
        snprintf(num, sizeof num, "%d", r + 1);
        display_str(4, y + 4, num, GREY, C_BG);
        for (int c = 0; c < 2; c++) {
            bool sel = !s_in_tabs && r == s_row && c == s_col;
            uint16_t bg = sel ? C_SEL : C_CELL;
            int x = c == 0 ? COL_X : COL_Y;
            display_fill_rect(x, y, COL_W, ROW_H - 2, bg);
            char t[28];
            if (sel && s_editing) snprintf(t, sizeof t, "%s_", s_buf);
            else if (r < s_rows) fmt(t, sizeof t, *cell(r, c));
            else t[0] = 0;
            if (!strcmp(t, "-")) t[0] = 0;
            display_str(x + 4, y + 3, t, C_TEXT, bg);
        }
    }
    if (s_msg[0]) footer(s_msg, RED);
    else footer(TR("OK:Invoer  <-:Wis  HOME:Terug", "OK:Enter  <-:Clear  HOME:Back"), YELLOW);
}

static void stat_line(int col, int row, const char *label, double v) {
    char t[32], num[20];
    fmt(num, sizeof num, v);
    snprintf(t, sizeof t, "%-7s %s", label, num);
    display_str(col ? 164 : 4, HEADER_H + 6 + row * 15, t, C_TEXT, C_BG);
}

static void draw_stats(void) {
    display_fill_rect(0, HEADER_H, LCD_WIDTH, LCD_HEIGHT - HEADER_H, C_BG);
    double xs[STATS_MAX], ys[STATS_MAX];
    stats1_t s;
    int n = collect_x(xs);
    if (!stats1(xs, n, &s)) {
        display_str(4, HEADER_H + 10, TR("Nog geen gegevens.", "No data yet."), WHITE, C_BG);
        footer("", YELLOW);
        return;
    }
    stat_line(0, 0, "n", s.n);
    stat_line(0, 1, TR("som", "sum"), s.sum);
    stat_line(0, 2, TR("gem.", "mean"), s.mean);
    stat_line(0, 3, TR("mediaan", "median"), s.median);
    stat_line(0, 4, "Q1", s.q1);
    stat_line(0, 5, "Q3", s.q3);
    stat_line(0, 6, "min", s.min);
    stat_line(0, 7, "max", s.max);
    stat_line(0, 8, TR("bereik", "range"), s.range);
    stat_line(0, 9, TR("sd pop", "sd pop"), s.sd_pop);
    stat_line(0, 10, TR("sd stp", "sd smp"), s.sd_sample);
    int np = collect_pairs(xs, ys);
    linreg_t lr;
    if (stats_linreg(xs, ys, np, &lr)) {
        display_str(164, HEADER_H + 6, TR("Regressie (X,Y)", "Regression (X,Y)"), YELLOW, C_BG);
        display_str(164, HEADER_H + 21, "y = ax + b", C_TEXT, C_BG);
        stat_line(1, 2, "a", lr.a);
        stat_line(1, 3, "b", lr.b);
        stat_line(1, 4, "r", lr.r);
        stat_line(1, 5, "r^2", lr.r2);
        stat_line(1, 6, TR("paren", "pairs"), np);
    } else {
        display_str(164, HEADER_H + 6, TR("Vul X en Y in", "Fill in X and Y"), GREY, C_BG);
        display_str(164, HEADER_H + 21, TR("voor regressie", "for regression"), GREY, C_BG);
    }
    footer(TR("L/R:Tabbladen  HOME:Terug", "L/R:Tabs  HOME:Back"), YELLOW);
}

#define PX0 30
#define PX1 (LCD_WIDTH - 10)
#define PY0 (HEADER_H + 8)
#define PY1 (LCD_HEIGHT - 28)

static void draw_scatter(const double *xs, const double *ys, int n, const linreg_t *lr) {
    stats1_t sx, sy;
    stats1(xs, n, &sx);
    stats1(ys, n, &sy);
    double x0 = sx.min, x1 = sx.max, y0 = sy.min, y1 = sy.max;
    if (x1 == x0) { x0 -= 1; x1 += 1; }
    if (y1 == y0) { y0 -= 1; y1 += 1; }
    double mx = (x1 - x0) * 0.08, my = (y1 - y0) * 0.08;
    x0 -= mx; x1 += mx; y0 -= my; y1 += my;
    #define SX(v) (PX0 + (int)lround(((v) - x0) / (x1 - x0) * (PX1 - PX0)))
    #define SY(v) (PY1 - (int)lround(((v) - y0) / (y1 - y0) * (PY1 - PY0)))
    display_rect(PX0, PY0, PX1 - PX0 + 1, PY1 - PY0 + 1, RGB(70,70,100));
    if (x0 < 0 && x1 > 0) display_vline(SX(0.0), PY0, PY1 - PY0, RGB(60,60,80));
    if (y0 < 0 && y1 > 0) display_hline(PX0, SY(0.0), PX1 - PX0, RGB(60,60,80));
    if (lr) {                                       /* the line, clipped to the box */
        for (int px = PX0; px <= PX1; px++) {
            double wx = x0 + (double)(px - PX0) / (PX1 - PX0) * (x1 - x0);
            int py = SY(lr->a * wx + lr->b);
            if (py >= PY0 && py <= PY1) display_pixel(px, py, RGB(255,160,40));
        }
    }
    for (int i = 0; i < n; i++) display_fill_rect(SX(xs[i]) - 1, SY(ys[i]) - 1, 3, 3, RGB(80,200,255));
    char t[48], a[16], b[16];
    fmt(a, sizeof a, x0 + mx); fmt(b, sizeof b, x1 - mx);
    snprintf(t, sizeof t, "x: %s .. %s", a, b);
    display_str(PX0, PY1 + 4, t, GREY, C_BG);
    #undef SX
    #undef SY
}

static void draw_box_hist(const double *xs, int n) {
    stats1_t s;
    stats1(xs, n, &s);
    double x0 = s.min, x1 = s.max;
    if (x1 == x0) { x0 -= 1; x1 += 1; }
    double m = (x1 - x0) * 0.05;
    x0 -= m; x1 += m;
    #define SX(v) (PX0 + (int)lround(((v) - x0) / (x1 - x0) * (PX1 - PX0)))
    /* Box plot */
    int by = PY0 + 4, bh = 24, mid = by + bh / 2;
    uint16_t c = RGB(80,200,255);
    display_hline(SX(s.min), mid, SX(s.q1) - SX(s.min), c);
    display_hline(SX(s.q3), mid, SX(s.max) - SX(s.q3), c);
    display_vline(SX(s.min), by + 6, bh - 12, c);
    display_vline(SX(s.max), by + 6, bh - 12, c);
    display_rect(SX(s.q1), by, SX(s.q3) - SX(s.q1) + 1, bh, c);
    display_vline(SX(s.median), by, bh, RGB(255,160,40));
    /* Histogram: Sturges' rule for the number of bins */
    int k = (int)ceil(log2((double)n)) + 1;
    if (k < 1) k = 1;
    if (k > 12) k = 12;
    int count[12] = {0}, top = 1;
    double w = (s.max - s.min) / k;
    for (int i = 0; i < n; i++) {
        int b = w > 0 ? (int)((xs[i] - s.min) / w) : 0;
        if (b >= k) b = k - 1;
        if (++count[b] > top) top = count[b];
    }
    int hy0 = by + bh + 14, hy1 = PY1;
    for (int b = 0; b < k; b++) {
        double lo = s.min + b * w, hi = w > 0 ? lo + w : s.max + 0.5;
        if (w == 0) lo = s.min - 0.5;
        int xa = SX(lo), xb = SX(hi);
        int h = count[b] * (hy1 - hy0) / top;
        if (h > 0) {
            display_fill_rect(xa + 1, hy1 - h, xb - xa - 1, h, RGB(60,130,255));
            display_rect(xa, hy1 - h, xb - xa + 1, h + 1, RGB(140,190,255));
        }
    }
    display_hline(PX0, hy1, PX1 - PX0, RGB(70,70,100));
    char t[64], a[16], b[16];
    fmt(a, sizeof a, s.min); fmt(b, sizeof b, s.max);
    snprintf(t, sizeof t, "min %s  max %s  %s %d", a, b, TR("staven", "bins"), k);
    display_str(PX0, PY1 + 4, t, GREY, C_BG);
    #undef SX
}

static void draw_plot(void) {
    display_fill_rect(0, HEADER_H, LCD_WIDTH, LCD_HEIGHT - HEADER_H, C_BG);
    double xs[STATS_MAX], ys[STATS_MAX];
    linreg_t lr;
    int np = collect_pairs(xs, ys);
    if (np >= 1) {
        bool line = stats_linreg(xs, ys, np, &lr);
        draw_scatter(xs, ys, np, line ? &lr : NULL);
    } else {
        int n = collect_x(xs);
        if (n == 0) display_str(4, HEADER_H + 10, TR("Nog geen gegevens.", "No data yet."), WHITE, C_BG);
        else draw_box_hist(xs, n);
    }
    footer(np >= 1 ? TR("Spreidingsdiagram + regressielijn", "Scatter plot + regression line")
                   : TR("Boxplot en histogram van X", "Box plot and histogram of X"), YELLOW);
}

void statistics_redraw(void) {
    display_fill(C_BG);
    draw_header();
    if (s_tab == TAB_DATA) draw_data();
    else if (s_tab == TAB_STATS) draw_stats();
    else draw_plot();
}

/* ── Editing ───────────────────────────────────────────────────── */
static void scroll_to_row(void) {
    if (s_row < s_top) s_top = s_row;
    if (s_row >= s_top + VIS_ROWS) s_top = s_row - VIS_ROWS + 1;
}

/* Store the edited cell. If it isn't a number the cell stays in edit
 * mode (with a message) so the typing can be corrected. */
static bool commit(void) {
    if (!s_editing) return true;
    if (s_blen == 0) { s_editing = false; return true; }
    double v;
    expr_status_t st = expr_eval(s_buf, 0.0, &v);
    if (st != EXPR_OK || isnan(v) || isinf(v)) {
        s_msg = st != EXPR_OK ? expr_error(st) : TR("geen getal", "not a number");
        return false;
    }
    if (s_row == s_rows) {                       /* the new row */
        if (s_rows >= STATS_MAX) { s_msg = TR("Lijst vol", "List full"); return false; }
        s_x[s_rows] = s_y[s_rows] = NAN;
        s_rows++;
    }
    s_editing = false;
    *cell(s_row, s_col) = v;
    s_dirty = true;
    return true;
}

static void data_key(key_code_t k) {
    s_msg = "";
    if (k == KEY_UP) {
        if (!commit()) { draw_data(); return; }
        if (s_row == 0) { s_in_tabs = true; draw_header(); draw_data(); return; }
        s_row--;
    } else if (k == KEY_DOWN) {
        if (!commit()) { draw_data(); return; }
        if (s_row < s_rows && s_row < STATS_MAX - 1) s_row++;
    } else if (k == KEY_LEFT || k == KEY_RIGHT) {
        if (!commit()) { draw_data(); return; }
        s_col = k == KEY_RIGHT;
    } else if (key_is_exe(k)) {
        if (s_editing) {
            if (!commit()) { draw_data(); return; }
            if (s_row < s_rows && s_row < STATS_MAX - 1) s_row++;
        } else {                                 /* edit the value that's there */
            s_editing = true;
            s_blen = 0; s_buf[0] = 0;
            if (s_row < s_rows && !isnan(*cell(s_row, s_col))) {
                snprintf(s_buf, sizeof s_buf, "%.10g", *cell(s_row, s_col));
                s_blen = (int)strlen(s_buf);
            }
        }
    } else if (k == KEY_BACKSPACE) {
        if (s_editing) {
            if (s_blen > 0) s_buf[--s_blen] = 0;
        } else if (s_row < s_rows) {
            *cell(s_row, s_col) = NAN;
            if (isnan(s_x[s_row]) && isnan(s_y[s_row])) remove_row(s_row);
            s_dirty = true;
        }
    } else if (k == KEY_SHIFT) {
        s_shift = !s_shift;
        return;
    } else {
        const char *t = expr_key_text(k, s_shift);
        if (!t) return;
        if (!s_editing) { s_editing = true; s_blen = 0; s_buf[0] = 0; }
        int n = (int)strlen(t);
        if (s_blen + n < (int)sizeof s_buf) { memcpy(s_buf + s_blen, t, (size_t)n + 1); s_blen += n; }
        s_shift = false;
    }
    if (s_row > s_rows) s_row = s_rows;
    scroll_to_row();
    draw_data();
}

void statistics_init(void) {
    load();
    s_tab = TAB_DATA;
    s_in_tabs = false;
    s_row = s_col = s_top = 0;
    s_editing = s_dirty = s_shift = false;
    s_msg = "";
}

void statistics_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    if (k == KEY_HOME || (k == KEY_BACK && !s_editing)) {
        commit();
        save();
        kernel_set_app(APP_HOME);
        return;
    }
    if (k == KEY_BACK) { s_editing = false; draw_data(); return; }   /* cancel the edit */

    if (s_in_tabs || s_tab != TAB_DATA) {
        if (k == KEY_LEFT || k == KEY_RIGHT) {
            s_tab = (s_tab + (k == KEY_RIGHT ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
            statistics_redraw();
        } else if (k == KEY_DOWN && s_in_tabs) {
            s_in_tabs = false;
            if (s_tab == TAB_DATA) { draw_header(); draw_data(); }
        } else if (k == KEY_UP && !s_in_tabs) {
            s_in_tabs = true;
            draw_header();
        }
        return;
    }
    data_key(k);
}

/* For tests */
int    statistics_rows(void) { return s_rows; }
double statistics_cell(int row, int col) { return *cell(row, col); }
void   statistics_save(void) { commit(); save(); }
