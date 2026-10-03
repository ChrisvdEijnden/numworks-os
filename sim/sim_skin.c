/* ================================================================
 * NumWorks OS — simulator: NumWorks' own simulator picture
 *
 * NumWorks' online simulator shows Epsilon in a picture of the
 * calculator. `make run-sim` downloads that picture and its layout
 * (where the screen and each key are) from NumWorks' public Epsilon
 * repository into build/sim/skin/; they are NumWorks' (all rights
 * reserved) and not part of this repository. This file puts our
 * screen and keys into it. Reading the WebP picture needs SDL2_image;
 * without it, or without the files, the window draws its own
 * calculator (sim_window.c).
 * ================================================================ */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_skin.h"

/* Epsilon's build crops the picture to the calculator (1005 x 1975 at
 * 93, 13: the shadow left out) and resizes that to layout.json's
 * "background" size; the layout's rectangles are in those units. The
 * online simulator shows the whole picture, shadow and all. */
#define CROP_X 93
#define CROP_Y 13
#define CROP_W 1005
#define CROP_H 1975
#define PAGE   0xFFFFFFU          /* behind the calculator */

/* ── layout.json ──────────────────────────────────────────────── */
static const struct { const char *name; key_code_t key; } NAMES[] = {
    {"Left", KEY_LEFT}, {"Up", KEY_UP}, {"Down", KEY_DOWN}, {"Right", KEY_RIGHT},
    {"OK", KEY_OK}, {"Back", KEY_BACK}, {"Home", KEY_HOME}, {"OnOff", KEY_ONOFF},
    {"Shift", KEY_SHIFT}, {"Alpha", KEY_ALPHA}, {"XNT", KEY_XNT}, {"Var", KEY_VAR},
    {"Toolbox", KEY_TOOLBOX}, {"Backspace", KEY_BACKSPACE}, {"Exp", KEY_EXP}, {"Ln", KEY_LN},
    {"Log", KEY_LOG}, {"Imaginary", KEY_IMAG}, {"Comma", KEY_COMMA}, {"Power", KEY_POW},
    {"Sine", KEY_SIN}, {"Cosine", KEY_COS}, {"Tangent", KEY_TAN}, {"Pi", KEY_PI},
    {"Sqrt", KEY_SQRT}, {"Square", KEY_SQUARE}, {"Seven", KEY_7}, {"Eight", KEY_8},
    {"Nine", KEY_9}, {"LeftParenthesis", KEY_LPAREN}, {"RightParenthesis", KEY_RPAREN},
    {"Four", KEY_4}, {"Five", KEY_5}, {"Six", KEY_6}, {"Multiplication", KEY_MUL},
    {"Division", KEY_DIV}, {"One", KEY_1}, {"Two", KEY_2}, {"Three", KEY_3}, {"Plus", KEY_PLUS},
    {"Minus", KEY_MINUS}, {"Zero", KEY_0}, {"Dot", KEY_DOT}, {"EE", KEY_EE}, {"Ans", KEY_ANS},
    {"EXE", KEY_EXE},
};

/* n numbers in the [ ] after p, which must come before end */
static const char *numbers(const char *p, const char *end, int *v, int n) {
    p = strchr(p, '[');
    if (!p || p > end) return NULL;
    p++;
    for (int i = 0; i < n; i++) {
        char *e;
        long x = strtol(p, &e, 10);
        if (e == p) return NULL;
        v[i] = (int)x;
        p = e;
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    }
    return *p == ']' ? p + 1 : NULL;
}

bool sim_skin_parse_layout(const char *json, skin_layout_t *out) {
    memset(out, 0, sizeof *out);
    const char *all = json + strlen(json), *p;
    int bg[4];
    if (!(p = strstr(json, "\"background\"")) || !numbers(p, all, bg, 4)) return false;
    if (!(p = strstr(json, "\"screen\"")) || !numbers(p, all, out->screen, 4)) return false;
    out->bg_w = bg[2];
    out->bg_h = bg[3];
    if (!(p = strstr(json, "\"keys\"")) || !(p = strchr(p, '{'))) return false;
    const char *end = strchr(p, '}');                   /* the keys hold no braces */
    if (!end) return false;
    while ((p = strchr(p, '"')) && p < end) {           /* "Name": [x, y, w, h] */
        const char *q = strchr(p + 1, '"');
        int v[4];
        const char *next = q ? numbers(q, end, v, 4) : NULL;
        if (!next) return false;
        for (unsigned i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
            if ((size_t)(q - p - 1) == strlen(NAMES[i].name) && !strncmp(p + 1, NAMES[i].name, (size_t)(q - p - 1)) &&
                out->nkeys < SKIN_MAX_KEYS)
                out->keys[out->nkeys++] = (skin_key_t){ NAMES[i].key, v[0], v[1], v[2], v[3] };
        p = next;
    }
    return out->bg_w > 0 && out->bg_h > 0 && out->screen[2] > 0 && out->nkeys > 0;
}

#ifdef SIM_SKIN
#include <SDL.h>
#include <SDL_image.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (n > 0 && n < 1 << 20 && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)n + 1))) {
        if (fread(buf, 1, (size_t)n, f) == (size_t)n) buf[n] = 0;
        else { free(buf); buf = NULL; }
    }
    fclose(f);
    return buf;
}

bool sim_skin_load(const char *dir, sim_skin_t *out) {
    char path[1024];
    skin_layout_t lay;
    snprintf(path, sizeof path, "%s/layout.json", dir);
    char *json = read_file(path);
    if (!json) return false;
    bool ok = sim_skin_parse_layout(json, &lay);
    free(json);
    if (!ok) { fprintf(stderr, "sim: %s: not a layout\n", path); return false; }

    snprintf(path, sizeof path, "%s/background-with-shadow.webp", dir);
    SDL_Surface *file = IMG_Load(path);
    if (!file) { fprintf(stderr, "sim: %s: %s\n", path, IMG_GetError()); return false; }
    SDL_Surface *img = SDL_ConvertSurfaceFormat(file, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(file);
    if (!img) return false;

    /* layout units per picture pixel, and picture pixels to ours: the
     * screen's 320 pixels become 640 */
    double s = fmin((double)lay.bg_w / CROP_W, (double)lay.bg_h / CROP_H);
    double k = 2.0 * 320 / (lay.screen[2] / s);
    out->w = (int)lround(img->w * k);
    out->h = (int)lround(img->h * k);
    out->px = malloc(sizeof(uint32_t) * (size_t)out->w * (size_t)out->h);
    if (!out->px) { SDL_FreeSurface(img); return false; }

    /* Scaled smoothly (bilinear, colours weighted by their opacity)
     * onto the page */
    SDL_LockSurface(img);
    const uint8_t *src = img->pixels;
    for (int y = 0; y < out->h; y++)
        for (int x = 0; x < out->w; x++) {
            double sx = (x + 0.5) / k - 0.5, sy = (y + 0.5) / k - 0.5;
            int x0 = (int)floor(sx), y0 = (int)floor(sy);
            double fx = sx - x0, fy = sy - y0, acc[4] = { 0, 0, 0, 0 };
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    int xi = x0 + i < 0 ? 0 : x0 + i >= img->w ? img->w - 1 : x0 + i;
                    int yj = y0 + j < 0 ? 0 : y0 + j >= img->h ? img->h - 1 : y0 + j;
                    const uint8_t *p = src + yj * img->pitch + xi * 4;
                    double wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy), a = p[3] / 255.0;
                    acc[0] += wgt * p[0] * a;
                    acc[1] += wgt * p[1] * a;
                    acc[2] += wgt * p[2] * a;
                    acc[3] += wgt * a;
                }
            uint32_t c = 0;
            for (int ch = 0; ch < 3; ch++) {
                double page = (PAGE >> (16 - 8 * ch)) & 0xFF;
                int v = (int)lround(acc[ch] + page * (1 - acc[3]));
                c = c << 8 | (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
            out->px[y * out->w + x] = c;
        }
    SDL_UnlockSurface(img);
    SDL_FreeSurface(img);

    /* Rectangles from layout units to ours */
    #define TX(v) ((CROP_X + (v) / s) * k)
    #define TY(v) ((CROP_Y + (v) / s) * k)
    out->scr_x = (int)lround(TX(lay.screen[0]));
    out->scr_y = (int)lround(TY(lay.screen[1]));
    out->nkeys = lay.nkeys;
    for (int i = 0; i < lay.nkeys; i++) {
        const skin_key_t *l = &lay.keys[i];
        out->keys[i] = (skin_key_t){ l->key, (int)lround(TX(l->x)), (int)lround(TY(l->y)),
                                     (int)lround(l->w / s * k), (int)lround(l->h / s * k) };
    }
    /* The LED, at the top right of the face, beside the name */
    out->led_x = (int)lround(TX(lay.bg_w * 0.88));
    out->led_y = (int)lround(TY(lay.screen[1] * 0.38));
    #undef TX
    #undef TY
    if (out->scr_x < 0 || out->scr_y < 0 || out->scr_x + 640 > out->w || out->scr_y + 480 > out->h) {
        free(out->px);
        out->px = NULL;
        return false;
    }
    return true;
}

bool sim_skin_supported(void) { return true; }
#else
bool sim_skin_load(const char *dir, sim_skin_t *out) {
    (void)dir; (void)out;
    return false;                       /* built without SDL2_image */
}

bool sim_skin_supported(void) { return false; }
#endif
