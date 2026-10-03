/* ================================================================
 * NumWorks OS — simulator: drawing the window
 *
 * A white calculator, upright: the screen (each pixel 2×2, dimmed
 * with the backlight) with the LED above it, then the keypad laid out
 * like the N0110's: the arrow ring, HOME and ON/OFF, OK and BACK, three
 * rows of function keys and four of digits. Above each key, in orange,
 * what it types with SHIFT and, in grey, with ALPHA, as this OS maps
 * them. Drawn in software into one 32-bit buffer, so the same picture
 * goes to the window and to screenshots; at twice the size the window
 * opens at, so it is sharp on a Retina screen. Labels use the OS's
 * own fonts.
 * ================================================================ */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_window.h"
#include "sim.h"
#include "../hal/font.h"
#include "../hal/backlight.h"

/* ── Layout ───────────────────────────────────────────────────── */
#define BODY_X 40
#define BODY_Y 30
#define BODY_W (SIM_WIN_W - 2 * BODY_X)
#define BODY_H (SIM_WIN_H - BODY_Y - 34)
#define SCR_X 80
#define SCR_Y 110
#define SCR_SCALE 2
#define LED_X (SCR_X + SIM_PANEL_W * SCR_SCALE - 12)
#define LED_Y 72

#define PAD_CX 205                        /* the arrow ring */
#define PAD_CY 775
#define PAD_RO 104
#define PAD_RI 38

#define FN_W 90                           /* function keys: 6 to a row */
#define FN_H 52
#define FN_X(c) (SCR_X + (c) * (FN_W + 20))
#define FN_Y(r) (918 + (r) * 82)
#define DG_W 112                          /* digit keys: 5 to a row */
#define DG_H 62
#define DG_X(c) (SCR_X + (c) * (DG_W + 20))
#define DG_Y(r) (1172 + (r) * 92)

enum { RING, ROUND, PILL, BLOCK };                       /* shapes */
enum { NO_ICON, I_HOME, I_POWER, I_DEL, I_SQRT, I_PI, I_MUL, I_DIV };
/* label: "e|x" is e with a raised x */
typedef struct { key_code_t key; short x, y, w, h; const char *label; char shape, icon; } pad_key_t;

static const pad_key_t PAD[] = {
    /* the ring: x, y its centre */
    { KEY_UP,    PAD_CX, PAD_CY, 0, 0, NULL, RING, 0 },
    { KEY_DOWN,  PAD_CX, PAD_CY, 0, 0, NULL, RING, 0 },
    { KEY_LEFT,  PAD_CX, PAD_CY, 0, 0, NULL, RING, 0 },
    { KEY_RIGHT, PAD_CX, PAD_CY, 0, 0, NULL, RING, 0 },
    /* round keys: x, y the centre, w the radius */
    { KEY_HOME,  400, 722, 46, 0, NULL, ROUND, I_HOME },
    { KEY_ONOFF, 400, 832, 34, 0, NULL, ROUND, I_POWER },
    { KEY_OK,    600, 722, 46, 0, "ok", ROUND, 0 },
    { KEY_BACK,  600, 832, 46, 0, "back", ROUND, 0 },
    { KEY_SHIFT, FN_X(0), FN_Y(0), FN_W, FN_H, "shift", PILL, 0 },
    { KEY_ALPHA, FN_X(1), FN_Y(0), FN_W, FN_H, "alpha", PILL, 0 },
    { KEY_XNT,   FN_X(2), FN_Y(0), FN_W, FN_H, "x,n,t", PILL, 0 },
    { KEY_VAR,   FN_X(3), FN_Y(0), FN_W, FN_H, "var", PILL, 0 },
    { KEY_TOOLBOX, FN_X(4), FN_Y(0), FN_W, FN_H, "toolbox", PILL, 0 },
    { KEY_BACKSPACE, FN_X(5), FN_Y(0), FN_W, FN_H, NULL, PILL, I_DEL },
    { KEY_EXP,   FN_X(0), FN_Y(1), FN_W, FN_H, "e|x", PILL, 0 },
    { KEY_LN,    FN_X(1), FN_Y(1), FN_W, FN_H, "ln", PILL, 0 },
    { KEY_LOG,   FN_X(2), FN_Y(1), FN_W, FN_H, "log", PILL, 0 },
    { KEY_IMAG,  FN_X(3), FN_Y(1), FN_W, FN_H, "i", PILL, 0 },
    { KEY_COMMA, FN_X(4), FN_Y(1), FN_W, FN_H, ",", PILL, 0 },
    { KEY_POW,   FN_X(5), FN_Y(1), FN_W, FN_H, "x|y", PILL, 0 },
    { KEY_SIN,   FN_X(0), FN_Y(2), FN_W, FN_H, "sin", PILL, 0 },
    { KEY_COS,   FN_X(1), FN_Y(2), FN_W, FN_H, "cos", PILL, 0 },
    { KEY_TAN,   FN_X(2), FN_Y(2), FN_W, FN_H, "tan", PILL, 0 },
    { KEY_PI,    FN_X(3), FN_Y(2), FN_W, FN_H, NULL, PILL, I_PI },
    { KEY_SQRT,  FN_X(4), FN_Y(2), FN_W, FN_H, NULL, PILL, I_SQRT },
    { KEY_SQUARE, FN_X(5), FN_Y(2), FN_W, FN_H, "x|2", PILL, 0 },
    { KEY_7, DG_X(0), DG_Y(0), DG_W, DG_H, "7", BLOCK, 0 },
    { KEY_8, DG_X(1), DG_Y(0), DG_W, DG_H, "8", BLOCK, 0 },
    { KEY_9, DG_X(2), DG_Y(0), DG_W, DG_H, "9", BLOCK, 0 },
    { KEY_LPAREN, DG_X(3), DG_Y(0), DG_W, DG_H, "(", BLOCK, 0 },
    { KEY_RPAREN, DG_X(4), DG_Y(0), DG_W, DG_H, ")", BLOCK, 0 },
    { KEY_4, DG_X(0), DG_Y(1), DG_W, DG_H, "4", BLOCK, 0 },
    { KEY_5, DG_X(1), DG_Y(1), DG_W, DG_H, "5", BLOCK, 0 },
    { KEY_6, DG_X(2), DG_Y(1), DG_W, DG_H, "6", BLOCK, 0 },
    { KEY_MUL, DG_X(3), DG_Y(1), DG_W, DG_H, NULL, BLOCK, I_MUL },
    { KEY_DIV, DG_X(4), DG_Y(1), DG_W, DG_H, NULL, BLOCK, I_DIV },
    { KEY_1, DG_X(0), DG_Y(2), DG_W, DG_H, "1", BLOCK, 0 },
    { KEY_2, DG_X(1), DG_Y(2), DG_W, DG_H, "2", BLOCK, 0 },
    { KEY_3, DG_X(2), DG_Y(2), DG_W, DG_H, "3", BLOCK, 0 },
    { KEY_PLUS, DG_X(3), DG_Y(2), DG_W, DG_H, "+", BLOCK, 0 },
    { KEY_MINUS, DG_X(4), DG_Y(2), DG_W, DG_H, "-", BLOCK, 0 },
    { KEY_0, DG_X(0), DG_Y(3), DG_W, DG_H, "0", BLOCK, 0 },
    { KEY_DOT, DG_X(1), DG_Y(3), DG_W, DG_H, ".", BLOCK, 0 },
    { KEY_EE, DG_X(2), DG_Y(3), DG_W, DG_H, "x10|x", BLOCK, 0 },
    { KEY_ANS, DG_X(3), DG_Y(3), DG_W, DG_H, "Ans", BLOCK, 0 },
    { KEY_EXE, DG_X(4), DG_Y(3), DG_W, DG_H, "EXE", BLOCK, 0 },
};
#define NPAD (sizeof(PAD) / sizeof(PAD[0]))

/* ── Colours ──────────────────────────────────────────────────── */
#define C_DESK     0xD9DCE1U
#define C_SHADOW   0xC2C5CBU
#define C_BODY     0xF8F8F7U
#define C_BEZEL    0x1C1D21U
#define C_KEY      0xFFFFFFU
#define C_KEY_DOWN 0xE2E4E8U
#define C_KEY_EDGE 0xCDD0D6U
#define C_KEY_SIDE 0xB9BDC4U          /* the key's lower edge */
#define C_LABEL    0x3B3E45U
#define C_SHIFT    0xF08A1CU          /* SHIFT: orange */
#define C_ALPHA    0x8D9097U          /* ALPHA: grey */
#define C_HOME     0xFFB734U
#define C_HOME_DN  0xE9A225U
#define C_POWER    0x45484FU
#define C_POWER_DN 0x5D6169U

/* ── Drawing primitives ───────────────────────────────────────── */
static uint32_t *s_fb;

static void fill(int x, int y, int w, int h, uint32_t c) {
    int x0 = x < 0 ? 0 : x, x1 = x + w > SIM_WIN_W ? SIM_WIN_W : x + w;
    for (int j = y < 0 ? 0 : y; j < y + h && j < SIM_WIN_H; j++)
        for (int i = x0; i < x1; i++) s_fb[j * SIM_WIN_W + i] = c;
}

/* a over b, a weight 0..256 */
static uint32_t mix(uint32_t a, uint32_t b, unsigned t) {
    uint32_t r = ((a >> 16 & 0xFF) * t + (b >> 16 & 0xFF) * (256 - t)) >> 8;
    uint32_t g = ((a >> 8 & 0xFF) * t + (b >> 8 & 0xFF) * (256 - t)) >> 8;
    uint32_t bl = ((a & 0xFF) * t + (b & 0xFF) * (256 - t)) >> 8;
    return r << 16 | g << 8 | bl;
}

static void blend(int x, int y, uint32_t c, unsigned t) {
    if (x < 0 || y < 0 || x >= SIM_WIN_W || y >= SIM_WIN_H || t == 0) return;
    uint32_t *p = &s_fb[y * SIM_WIN_W + x];
    *p = t >= 256 ? c : mix(c, *p, t);
}

/* A rectangle with corners rounded by r pixels, edges smoothed */
static void fill_round(int x, int y, int w, int h, int r, uint32_t c) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    fill(x, y + r, w, h - 2 * r, c);
    fill(x + r, y, w - 2 * r, r, c);
    fill(x + r, y + h - r, w - 2 * r, r, c);
    for (int j = 0; j < r; j++)                           /* the corners: how much of each pixel is inside */
        for (int i = 0; i < r; i++) {
            double cover = r - hypot(r - (i + 0.5), r - (j + 0.5)) + 0.5;
            if (cover <= 0) continue;
            unsigned t = cover >= 1 ? 256 : (unsigned)(cover * 256);
            blend(x + i, y + j, c, t);
            blend(x + w - 1 - i, y + j, c, t);
            blend(x + i, y + h - 1 - j, c, t);
            blend(x + w - 1 - i, y + h - 1 - j, c, t);
        }
}

/* A disc, its edge smoothed */
static void disc(double cx, double cy, double r, uint32_t c) {
    for (int j = (int)(cy - r - 1); j <= (int)(cy + r + 1); j++)
        for (int i = (int)(cx - r - 1); i <= (int)(cx + r + 1); i++) {
            double d = r - hypot(i + 0.5 - cx, j + 0.5 - cy);
            if (d >= 1) blend(i, j, c, 256);
            else if (d > 0) blend(i, j, c, (unsigned)(d * 256));
        }
}

/* A line w pixels thick, with round ends */
static void line(double x0, double y0, double x1, double y1, double w, uint32_t c) {
    double len = hypot(x1 - x0, y1 - y0), r = w / 2;
    int xa = (int)(fmin(x0, x1) - r - 1), xb = (int)(fmax(x0, x1) + r + 1);
    int ya = (int)(fmin(y0, y1) - r - 1), yb = (int)(fmax(y0, y1) + r + 1);
    for (int j = ya; j <= yb; j++)
        for (int i = xa; i <= xb; i++) {
            double px = i + 0.5, py = j + 0.5, t = 0;
            if (len > 0) t = fmax(0, fmin(1, ((px - x0) * (x1 - x0) + (py - y0) * (y1 - y0)) / (len * len)));
            double d = r - hypot(px - (x0 + t * (x1 - x0)), py - (y0 + t * (y1 - y0)));
            if (d >= 1) blend(i, j, c, 256);
            else if (d > 0) blend(i, j, c, (unsigned)(d * 256));
        }
}

/* ── Text in the OS's fonts, scaled smoothly ──────────────────── */
static double glyph_at(const font_t *f, const uint8_t *g, int x, int y) {
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return 0;
    uint8_t b = g[y * ((f->w + 1) / 2) + (x >> 1)];
    return ((x & 1) ? (b & 15) : (b >> 4)) / 15.0;
}

static void text(double x, double y, const char *s, int n, const font_t *f, double scale, uint32_t c) {
    int rb = (f->w + 1) / 2;
    for (int k = 0; k < n && s[k]; k++, x += f->w * scale) {
        unsigned char ch = (unsigned char)s[k];
        if (ch < 32 || ch > 126) ch = '?';
        const uint8_t *g = f->data + (ch - 32) * f->h * rb;
        for (int j = 0; j < (int)ceil(f->h * scale); j++)
            for (int i = 0; i < (int)ceil(f->w * scale); i++) {
                double sx = (i + 0.5) / scale - 0.5, sy = (j + 0.5) / scale - 0.5;
                int ix = (int)floor(sx), iy = (int)floor(sy);
                double fx = sx - ix, fy = sy - iy;
                double a = glyph_at(f, g, ix, iy) * (1 - fx) * (1 - fy) + glyph_at(f, g, ix + 1, iy) * fx * (1 - fy) +
                           glyph_at(f, g, ix, iy + 1) * (1 - fx) * fy + glyph_at(f, g, ix + 1, iy + 1) * fx * fy;
                blend((int)x + i, (int)y + j, c, (unsigned)(a * 256));
            }
    }
}

static double text_w(int n, const font_t *f, double scale) { return n * f->w * scale; }

/* A label centred at cx, cy, at most w wide: "e|x" is e with a raised x */
static void label(double cx, double cy, const char *s, double scale, double w, uint32_t c) {
    const font_t *f = &font_large;
    const char *bar = strchr(s, '|');
    int nb = bar ? (int)(bar - s) : (int)strlen(s), ne = bar ? (int)strlen(bar + 1) : 0;
    double sup = 0.65;
    double tw = text_w(nb, f, scale) + text_w(ne, f, scale * sup);
    if (tw > w) { scale *= w / tw; tw = w; }
    double x = cx - tw / 2, y = cy - f->h * scale / 2;
    text(x, y, s, nb, f, scale, c);
    if (bar) text(x + text_w(nb, f, scale), y - f->h * scale * 0.18, bar + 1, ne, f, scale * sup, c);
}

/* ── Icons, drawn with lines ──────────────────────────────────── */
static void icon(int which, double cx, double cy, uint32_t c, uint32_t bg) {
    switch (which) {
    case I_HOME:                                          /* a house */
        line(cx - 15, cy - 1, cx, cy - 14, 4, c);
        line(cx, cy - 14, cx + 15, cy - 1, 4, c);
        fill((int)cx - 10, (int)cy - 3, 20, 17, c);
        fill((int)cx - 3, (int)cy + 5, 6, 9, bg);
        break;
    case I_POWER:                                         /* a ring, open at the top, and a bar */
        for (int a = 40; a <= 320; a += 4) {
            double t = (a - 90) * M_PI / 180;
            disc(cx + 11 * cos(t), cy - 11 * sin(t), 1.6, c);
        }
        line(cx, cy - 15, cx, cy - 3, 3.5, c);
        break;
    case I_DEL:                                           /* the backspace key: an arrow with an x */
        for (int j = -11; j <= 11; j++) fill((int)cx - 8 + abs(j), (int)cy + j, 26 - abs(j), 1, c);
        line(cx + 1, cy - 5, cx + 11, cy + 5, 2.5, bg);
        line(cx + 1, cy + 5, cx + 11, cy - 5, 2.5, bg);
        break;
    case I_SQRT:                                          /* a radical over an x */
        line(cx - 22, cy + 2, cx - 17, cy - 1, 2.5, c);
        line(cx - 17, cy - 1, cx - 11, cy + 13, 2.5, c);
        line(cx - 11, cy + 13, cx - 4, cy - 14, 2.5, c);
        line(cx - 4, cy - 14, cx + 20, cy - 14, 2.5, c);
        text(cx - 1, cy - 11, "x", 1, &font_large, 1.3, c);
        break;
    case I_PI:                                            /* pi */
        line(cx - 13, cy - 9, cx + 13, cy - 9, 3, c);
        line(cx - 6, cy - 9, cx - 8, cy + 11, 3, c);
        line(cx + 6, cy - 9, cx + 6, cy + 8, 3, c);
        line(cx + 6, cy + 8, cx + 10, cy + 11, 3, c);
        break;
    case I_MUL:                                           /* times */
        line(cx - 9, cy - 9, cx + 9, cy + 9, 3.5, c);
        line(cx - 9, cy + 9, cx + 9, cy - 9, 3.5, c);
        break;
    case I_DIV:                                           /* divided by */
        line(cx - 12, cy, cx + 12, cy, 3.5, c);
        disc(cx, cy - 9, 3, c);
        disc(cx, cy + 9, 3, c);
        break;
    }
}

/* ── What SHIFT and ALPHA make of a key, as this OS maps it ─────── */
static void shift_label(key_code_t k, char *out) {
    const char *t = NULL;
    switch (k) {
    case KEY_SIN: t = "asin"; break;
    case KEY_COS: t = "acos"; break;
    case KEY_TAN: t = "atan"; break;
    case KEY_SQRT: t = "cbrt"; break;
    case KEY_LN: t = "exp"; break;
    case KEY_LOG: t = "10^"; break;
    case KEY_EXP: t = "e"; break;
    default: break;
    }
    if (t) { strcpy(out, t); return; }
    char plain = key_to_char(k, false, false), sh = key_to_char(k, true, false);
    out[0] = sh && sh != plain ? sh : 0;
    out[1] = 0;
}

static void alpha_label(key_code_t k, char *out) {
    char c = key_to_char(k, false, true);
    out[0] = c > ' ' ? c : 0;
    out[1] = 0;
}

/* ── The keys ─────────────────────────────────────────────────── */
static bool ring_down(void) {
    return sim_key_down(KEY_UP) || sim_key_down(KEY_DOWN) || sim_key_down(KEY_LEFT) || sim_key_down(KEY_RIGHT);
}

/* The arrow ring: four quarters, the pressed one shaded */
static void draw_ring(void) {
    disc(PAD_CX, PAD_CY + 4, PAD_RO + 1, C_KEY_SIDE);
    disc(PAD_CX, PAD_CY, PAD_RO + 1, C_KEY_EDGE);
    disc(PAD_CX, PAD_CY, PAD_RO - 1, C_KEY);
    for (int j = -PAD_RO; j <= PAD_RO; j++)
        for (int i = -PAD_RO; i <= PAD_RO; i++) {
            double d = hypot(i + 0.5, j + 0.5);
            if (d > PAD_RO - 1.5 || d < PAD_RI) continue;
            key_code_t k = abs(i) > abs(j) ? (i < 0 ? KEY_LEFT : KEY_RIGHT) : (j < 0 ? KEY_UP : KEY_DOWN);
            if (fabs(fabs(i + 0.5) - fabs(j + 0.5)) < 1.2) blend(PAD_CX + i, PAD_CY + j, C_KEY_EDGE, 256);
            else if (sim_key_down(k)) blend(PAD_CX + i, PAD_CY + j, C_KEY_DOWN, 256);
        }
    disc(PAD_CX, PAD_CY, PAD_RI + 1.5, C_KEY_EDGE);
    disc(PAD_CX, PAD_CY, PAD_RI, C_BODY);
    static const struct { int dx, dy; } DIRS[4] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
    for (int d = 0; d < 4; d++) {                         /* the arrows */
        double ax = PAD_CX + DIRS[d].dx * 72, ay = PAD_CY + DIRS[d].dy * 72;
        double px = -DIRS[d].dy, py = DIRS[d].dx;          /* across the arrow */
        for (int s = 0; s <= 10; s++) {
            double h = 11 - s;
            line(ax + DIRS[d].dx * (s - 5) + px * h, ay + DIRS[d].dy * (s - 5) + py * h,
                 ax + DIRS[d].dx * (s - 5) - px * h, ay + DIRS[d].dy * (s - 5) - py * h, 1.5, 0x6B6F78);
        }
    }
}

/* above: also what SHIFT and ALPHA type, drawn once with the rest */
static void draw_key(const pad_key_t *k, bool above) {
    bool down = sim_key_down(k->key);
    int dy = down ? 2 : 0;
    char sh[8], al[2];
    switch (k->shape) {
    case ROUND: {
        uint32_t face = k->key == KEY_HOME ? (down ? C_HOME_DN : C_HOME)
                      : k->key == KEY_ONOFF ? (down ? C_POWER_DN : C_POWER) : (down ? C_KEY_DOWN : C_KEY);
        disc(k->x, k->y + 4, k->w + 1, C_KEY_SIDE);
        disc(k->x, k->y + dy, k->w + 1, k->key == KEY_HOME || k->key == KEY_ONOFF ? face : C_KEY_EDGE);
        disc(k->x, k->y + dy, k->w - 1, face);
        if (k->icon) icon(k->icon, k->x, k->y + dy, 0xFFFFFF, face);
        else label(k->x, k->y + dy, k->label, 1.5, k->w * 1.6, C_LABEL);
        return;
    }
    case PILL: case BLOCK: {
        int r = k->shape == PILL ? k->h / 2 : 16;
        fill_round(k->x - 2, k->y + 2, k->w + 4, k->h + 4, r + 2, C_KEY_SIDE);
        fill_round(k->x - 2, k->y - 2 + dy, k->w + 4, k->h + 4, r + 2, C_KEY_EDGE);
        fill_round(k->x, k->y + dy, k->w, k->h, r, down ? C_KEY_DOWN : C_KEY);
        double cx = k->x + k->w / 2.0, cy = k->y + k->h / 2.0 + dy;
        uint32_t c = k->key == KEY_SHIFT ? C_SHIFT : k->key == KEY_ALPHA ? C_ALPHA : C_LABEL;
        if (k->icon) icon(k->icon, cx, cy, C_LABEL, down ? C_KEY_DOWN : C_KEY);
        else label(cx, cy, k->label, k->shape == PILL ? 1.4 : 1.8, k->w - 18, c);
        /* above it: SHIFT on the left, ALPHA on the right */
        if (!above) return;
        shift_label(k->key, sh);
        alpha_label(k->key, al);
        double s = 1.45, ty = k->y - 6 - font_small.h * s;
        if (sh[0]) text(k->x + 2, ty, sh, 8, &font_small, s, C_SHIFT);
        if (al[0]) text(k->x + k->w - 2 - text_w(1, &font_small, s), ty, al, 1, &font_small, s, C_ALPHA);
        return;
    }
    }
}

/* ── The window ───────────────────────────────────────────────── */
static uint32_t s_panel[SIM_PANEL_H][SIM_PANEL_W];
static uint32_t s_base[SIM_WIN_H * SIM_WIN_W];         /* the calculator with no key down */
static bool s_have_base;

static uint32_t dim(uint32_t c, unsigned f) {   /* f: 0..256 */
    return (((c >> 16 & 0xFF) * f >> 8) << 16) | (((c >> 8 & 0xFF) * f >> 8) << 8) | ((c & 0xFF) * f >> 8);
}

static unsigned backlight_factor(void) {
    int level = sim_backlight();
    return level < 0 ? 0 : level >= 12 ? 256 : 96 + 160U * (unsigned)level / 12;   /* the default level and up: full */
}

static void draw_screen(void) {
    sim_panel_read(s_panel);
    unsigned f = backlight_factor();
    for (int y = 0; y < SIM_PANEL_H; y++)
        for (int x = 0; x < SIM_PANEL_W; x++) {
            uint32_t c = dim(s_panel[y][x], f);
            uint32_t *p = s_fb + (SCR_Y + y * SCR_SCALE) * SIM_WIN_W + SCR_X + x * SCR_SCALE;
            p[0] = p[1] = p[SIM_WIN_W] = p[SIM_WIN_W + 1] = c;
        }
}

static void draw_base(void) {
    fill(0, 0, SIM_WIN_W, SIM_WIN_H, C_DESK);
    fill_round(BODY_X - 2, BODY_Y + 6, BODY_W + 4, BODY_H + 4, 72, C_SHADOW);
    fill_round(BODY_X, BODY_Y, BODY_W, BODY_H, 70, C_BODY);
    fill_round(SCR_X - 16, SCR_Y - 16, SIM_PANEL_W * SCR_SCALE + 32, SIM_PANEL_H * SCR_SCALE + 32, 14, C_BEZEL);
    draw_ring();
    for (unsigned i = 0; i < NPAD; i++)
        if (PAD[i].shape != RING) draw_key(&PAD[i], true);
}

void sim_window_draw(uint32_t *fb) {
    s_fb = s_base;
    if (!s_have_base) { draw_base(); s_have_base = true; }
    memcpy(fb, s_base, sizeof s_base);
    s_fb = fb;
    /* keys that are down, drawn again on a clean patch of the body */
    if (ring_down()) draw_ring();
    for (unsigned i = 0; i < NPAD; i++) {
        const pad_key_t *k = &PAD[i];
        if (k->shape == RING || !sim_key_down(k->key)) continue;
        if (k->shape == ROUND) disc(k->x, k->y + 2, k->w + 4, C_BODY);
        else fill(k->x - 3, k->y - 3, k->w + 6, k->h + 10, C_BODY);
        draw_key(k, false);
    }
    draw_screen();
    uint32_t led = sim_led_rgb();                         /* the LED, above the screen */
    disc(LED_X, LED_Y, 8, 0xA9ACB2);
    disc(LED_X, LED_Y, 6, led ? led : 0xDADCE0);
}

key_code_t sim_window_key_at(int x, int y) {
    double rd = hypot(x + 0.5 - PAD_CX, y + 0.5 - PAD_CY);
    if (rd >= PAD_RI && rd <= PAD_RO) {
        int i = x - PAD_CX, j = y - PAD_CY;
        return abs(i) > abs(j) ? (i < 0 ? KEY_LEFT : KEY_RIGHT) : (j < 0 ? KEY_UP : KEY_DOWN);
    }
    for (unsigned i = 0; i < NPAD; i++) {
        const pad_key_t *k = &PAD[i];
        if (k->shape == ROUND) {
            if (hypot(x - k->x, y - k->y) <= k->w + 4) return k->key;
        } else if (k->shape != RING && x >= k->x - 4 && x < k->x + k->w + 4 && y >= k->y - 4 &&
                   y < k->y + k->h + 6) {
            return k->key;
        }
    }
    return KEY_NONE;
}

/* 24-bit BMP, bottom-up: no library needed */
static void le(uint8_t *p, uint32_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i)); }

bool sim_save_bmp(const char *path, const uint32_t *px, int w, int h, int stride) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return false; }
    int row = (w * 3 + 3) & ~3;
    uint8_t hdr[54] = { 'B', 'M' };
    le(hdr + 2, (uint32_t)(54 + row * h), 4);
    le(hdr + 10, 54, 4);
    le(hdr + 14, 40, 4);
    le(hdr + 18, (uint32_t)w, 4);
    le(hdr + 22, (uint32_t)h, 4);
    le(hdr + 26, 1, 2);
    le(hdr + 28, 24, 2);
    le(hdr + 34, (uint32_t)(row * h), 4);
    fwrite(hdr, 1, sizeof hdr, f);
    uint8_t line_buf[SIM_WIN_W * 3 + 4] = { 0 };
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) le(line_buf + 3 * x, px[y * stride + x], 3);
        fwrite(line_buf, 1, (size_t)row, f);
    }
    return fclose(f) == 0;
}

void sim_screen_pixels(uint32_t out[SIM_PANEL_H][SIM_PANEL_W]) {
    sim_panel_read(out);
    unsigned f = backlight_factor();
    for (int y = 0; y < SIM_PANEL_H; y++)
        for (int x = 0; x < SIM_PANEL_W; x++) out[y][x] = dim(out[y][x], f);
}
