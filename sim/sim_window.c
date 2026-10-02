/* ================================================================
 * NumWorks OS — simulator: drawing the window
 *
 * The screen (each pixel 2×2, dimmed with the backlight), the keypad
 * laid out like the calculator's, and the LED. Drawn in software into
 * one 32-bit buffer, so the same picture goes to the window and to
 * screenshots. Labels use the OS's own font.
 * ================================================================ */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sim_window.h"
#include "sim.h"
#include "../hal/font.h"
#include "../hal/backlight.h"

/* ── Layout ───────────────────────────────────────────────────── */
#define SCR_X 28
#define SCR_Y 28
#define SCR_SCALE 2
#define PAD_X 712
#define PAD_W 336

enum { NAV, FUNC, DIGIT, ACCENT };
typedef struct { key_code_t key; short x, y, w, h; const char *label; char style; } pad_key_t;

#define R6(row) (186 + (row) * 44)          /* rows of six keys */
#define R5(row) (186 + 3 * 44 + (row) * 48) /* rows of five */
#define C6(col) (PAD_X + (col) * 57)
#define C5(col) (PAD_X + (col) * 68)

static const pad_key_t PAD[] = {
    /* arrows, HOME and ON/OFF, OK and BACK */
    { KEY_UP,    PAD_X + 46,  48, 48, 36, "^",    NAV },
    { KEY_LEFT,  PAD_X + 0,   88, 48, 36, "<",    NAV },
    { KEY_RIGHT, PAD_X + 92,  88, 48, 36, ">",    NAV },
    { KEY_DOWN,  PAD_X + 46, 128, 48, 36, "v",    NAV },
    { KEY_HOME,  PAD_X + 160, 48, 72, 36, "HOME", NAV },
    { KEY_ONOFF, PAD_X + 160,128, 72, 36, "ON/OFF", NAV },
    { KEY_OK,    PAD_X + 252, 48, 84, 36, "OK",   NAV },
    { KEY_BACK,  PAD_X + 252,128, 84, 36, "BACK", NAV },
    { KEY_SHIFT, C6(0), R6(0), 51, 38, "shift", ACCENT },
    { KEY_ALPHA, C6(1), R6(0), 51, 38, "alpha", ACCENT },
    { KEY_XNT,   C6(2), R6(0), 51, 38, "x,n,t", FUNC },
    { KEY_VAR,   C6(3), R6(0), 51, 38, "var",   FUNC },
    { KEY_TOOLBOX, C6(4), R6(0), 51, 38, "tools", FUNC },
    { KEY_BACKSPACE, C6(5), R6(0), 51, 38, "DEL", FUNC },
    { KEY_EXP,   C6(0), R6(1), 51, 38, "e^x",  FUNC },
    { KEY_LN,    C6(1), R6(1), 51, 38, "ln",   FUNC },
    { KEY_LOG,   C6(2), R6(1), 51, 38, "log",  FUNC },
    { KEY_IMAG,  C6(3), R6(1), 51, 38, "i",    FUNC },
    { KEY_COMMA, C6(4), R6(1), 51, 38, ",",    FUNC },
    { KEY_POW,   C6(5), R6(1), 51, 38, "x^y",  FUNC },
    { KEY_SIN,   C6(0), R6(2), 51, 38, "sin",  FUNC },
    { KEY_COS,   C6(1), R6(2), 51, 38, "cos",  FUNC },
    { KEY_TAN,   C6(2), R6(2), 51, 38, "tan",  FUNC },
    { KEY_PI,    C6(3), R6(2), 51, 38, "pi",   FUNC },
    { KEY_SQRT,  C6(4), R6(2), 51, 38, "sqrt", FUNC },
    { KEY_SQUARE, C6(5), R6(2), 51, 38, "x^2", FUNC },
    { KEY_7, C5(0), R5(0), 60, 42, "7", DIGIT }, { KEY_8, C5(1), R5(0), 60, 42, "8", DIGIT },
    { KEY_9, C5(2), R5(0), 60, 42, "9", DIGIT }, { KEY_LPAREN, C5(3), R5(0), 60, 42, "(", DIGIT },
    { KEY_RPAREN, C5(4), R5(0), 60, 42, ")", DIGIT },
    { KEY_4, C5(0), R5(1), 60, 42, "4", DIGIT }, { KEY_5, C5(1), R5(1), 60, 42, "5", DIGIT },
    { KEY_6, C5(2), R5(1), 60, 42, "6", DIGIT }, { KEY_MUL, C5(3), R5(1), 60, 42, "*", DIGIT },
    { KEY_DIV, C5(4), R5(1), 60, 42, "/", DIGIT },
    { KEY_1, C5(0), R5(2), 60, 42, "1", DIGIT }, { KEY_2, C5(1), R5(2), 60, 42, "2", DIGIT },
    { KEY_3, C5(2), R5(2), 60, 42, "3", DIGIT }, { KEY_PLUS, C5(3), R5(2), 60, 42, "+", DIGIT },
    { KEY_MINUS, C5(4), R5(2), 60, 42, "-", DIGIT },
    { KEY_0, C5(0), R5(3), 60, 42, "0", DIGIT }, { KEY_DOT, C5(1), R5(3), 60, 42, ".", DIGIT },
    { KEY_EE, C5(2), R5(3), 60, 42, "x10^", DIGIT }, { KEY_ANS, C5(3), R5(3), 60, 42, "Ans", DIGIT },
    { KEY_EXE, C5(4), R5(3), 60, 42, "EXE", ACCENT },
};
#define NPAD (sizeof(PAD) / sizeof(PAD[0]))

/* ── Colours ──────────────────────────────────────────────────── */
#define C_BG       0x24262BU
#define C_BEZEL    0x0D0E10U
#define C_TEXT     0xE8E9EBU
#define C_DIM      0x9A9DA3U
#define C_ALPHA    0xF7A600U   /* the letters ALPHA types */
#define C_SHIFT    0xFFD54AU

static const struct { uint32_t fill, down, text; } STYLE[] = {
    [NAV]    = { 0x3D4048, 0x5A5E68, C_TEXT },
    [FUNC]   = { 0x4B4F58, 0x6A6F7A, C_TEXT },
    [DIGIT]  = { 0xECEDEF, 0xBFC2C7, 0x1E2024 },
    [ACCENT] = { 0x4B4F58, 0x6A6F7A, C_SHIFT },
};

/* ── Drawing primitives ───────────────────────────────────────── */
static uint32_t *s_fb;

static void fill(int x, int y, int w, int h, uint32_t c) {
    for (int j = y < 0 ? 0 : y; j < y + h && j < SIM_WIN_H; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < SIM_WIN_W; i++)
            s_fb[j * SIM_WIN_W + i] = c;
}

/* A rectangle with corners rounded by r pixels */
static void fill_round(int x, int y, int w, int h, int r, uint32_t c) {
    for (int j = 0; j < h; j++) {
        double dy = j < r ? r - j - 0.5 : j >= h - r ? j - (h - r) + 0.5 : 0;
        int inset = dy > 0 ? (int)(r - sqrt((double)r * r - dy * dy) + 0.5) : 0;
        fill(x + inset, y + j, w - 2 * inset, 1, c);
    }
}

static void text(int x, int y, const char *s, int scale, uint32_t c) {
    for (; *s; s++, x += FONT_W * scale) {
        const uint8_t *g = font_get_char(*s);
        for (int row = 0; row < FONT_H; row++)
            for (int col = 0; col < FONT_W; col++)
                if (g[row] & (0x80 >> col)) fill(x + col * scale, y + row * scale, scale, scale, c);
    }
}

static int text_w(const char *s, int scale) { return (int)strlen(s) * FONT_W * scale; }

static void disc(int cx, int cy, int r, uint32_t c) {
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r + r) fill(cx + i, cy + j, 1, 1, c);
}

static uint32_t dim(uint32_t c, unsigned f) {   /* f: 0..256 */
    return (((c >> 16 & 0xFF) * f >> 8) << 16) | (((c >> 8 & 0xFF) * f >> 8) << 8) | ((c & 0xFF) * f >> 8);
}

/* ── The window ───────────────────────────────────────────────── */
static uint32_t s_panel[SIM_PANEL_H][SIM_PANEL_W];

static void draw_screen(void) {
    fill_round(SCR_X - 14, SCR_Y - 14, SIM_PANEL_W * SCR_SCALE + 28, SIM_PANEL_H * SCR_SCALE + 28, 10, C_BEZEL);
    sim_panel_read(s_panel);
    int level = sim_backlight();
    unsigned f = level < 0 ? 0 : 72 + 184U * (unsigned)level / BACKLIGHT_MAX;
    for (int y = 0; y < SIM_PANEL_H; y++)
        for (int x = 0; x < SIM_PANEL_W; x++) {
            uint32_t c = dim(s_panel[y][x], f);
            uint32_t *p = s_fb + (SCR_Y + y * SCR_SCALE) * SIM_WIN_W + SCR_X + x * SCR_SCALE;
            p[0] = p[1] = p[SIM_WIN_W] = p[SIM_WIN_W + 1] = c;
        }
}

static void draw_key(const pad_key_t *k) {
    bool down = sim_key_down(k->key);
    fill_round(k->x, k->y + (down ? 1 : 0), k->w, k->h, 7, down ? STYLE[(int)k->style].down : STYLE[(int)k->style].fill);
    int scale = text_w(k->label, 2) <= k->w - 8 ? 2 : 1;
    uint32_t c = STYLE[(int)k->style].text;
    if (k->key == KEY_ALPHA) c = C_ALPHA;
    text(k->x + (k->w - text_w(k->label, scale)) / 2, k->y + (k->h - FONT_H * scale) / 2 + (down ? 1 : 0),
         k->label, scale, c);
    char letter[2] = { key_to_char(k->key, false, true), 0 };   /* what ALPHA types */
    if (letter[0] && letter[0] != ' ') text(k->x + k->w - 9, k->y + 3, letter, 1, C_ALPHA);
}

void sim_window_draw(uint32_t *fb, const char *status) {
    s_fb = fb;
    fill(0, 0, SIM_WIN_W, SIM_WIN_H, C_BG);
    draw_screen();
    for (unsigned i = 0; i < NPAD; i++) draw_key(&PAD[i]);

    uint32_t led = sim_led_rgb();                /* the LED, above OK */
    disc(PAD_X + 294, 22, 8, 0x0D0E10);
    disc(PAD_X + 294, 22, 6, led ? led : 0x3A3C42);
    text(PAD_X + 252, 18, "LED", 1, C_DIM);

    int y = SCR_Y + SIM_PANEL_H * SCR_SCALE + 26;
    text(SCR_X - 6, y, "Keys: arrows, Enter = EXE, Tab = OK, Esc = BACK, Backspace = DEL, F1/Home = HOME,", 1, C_DIM);
    text(SCR_X - 6, y + 12, "F2/End = ON/OFF, Ctrl = shift, Alt/Option = alpha; characters press the key that", 1, C_DIM);
    text(SCR_X - 6, y + 24, "carries them (letters: their alpha key). Or click the keys.", 1, C_DIM);
    if (status) text(SCR_X - 6, y + 40, status, 1, C_DIM);
}

key_code_t sim_window_key_at(int x, int y) {
    for (unsigned i = 0; i < NPAD; i++)
        if (x >= PAD[i].x && x < PAD[i].x + PAD[i].w && y >= PAD[i].y && y < PAD[i].y + PAD[i].h)
            return PAD[i].key;
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
    uint8_t line[SIM_WIN_W * 3 + 4] = { 0 };
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) le(line + 3 * x, px[y * stride + x], 3);
        fwrite(line, 1, (size_t)row, f);
    }
    return fclose(f) == 0;
}

void sim_screen_pixels(uint32_t out[SIM_PANEL_H][SIM_PANEL_W]) {
    sim_panel_read(out);
    int level = sim_backlight();
    unsigned f = level < 0 ? 0 : 72 + 184U * (unsigned)level / BACKLIGHT_MAX;
    for (int y = 0; y < SIM_PANEL_H; y++)
        for (int x = 0; x < SIM_PANEL_W; x++) out[y][x] = dim(out[y][x], f);
}
