/* ================================================================
 * NumWorks OS — `kandinsky` module for MicroPython
 *
 * The drawing module of NumWorks' own Python, so scripts written for
 * the calculator's stock firmware run unchanged:
 *   kandinsky.fill_rect(x, y, width, height, color)
 *   kandinsky.set_pixel(x, y, color)
 *   kandinsky.get_pixel(x, y) -> (r, g, b)
 *   kandinsky.draw_string(text, x, y[, color[, background]])
 *   kandinsky.color(r, g, b) -> (r, g, b)
 * A color is an (r, g, b) tuple or list, a name ("red", "blue", ...)
 * or "#rrggbb". Text is black on white unless given. The whole 320x240
 * screen is the canvas (NumWorks' is 320x222, below its status bar).
 * ================================================================ */
#include "py/runtime.h"
#include "py/objstr.h"
#include <string.h>
#include "../../../hal/display.h"
#include "../../mp_port.h"

typedef struct { const char *name; uint8_t r, g, b; } named_t;
static const named_t NAMES[] = {
    {"black", 0, 0, 0},       {"white", 255, 255, 255}, {"red", 255, 0, 0},
    {"green", 0, 255, 0},     {"blue", 0, 0, 255},      {"yellow", 255, 255, 0},
    {"cyan", 0, 255, 255},    {"magenta", 255, 0, 255}, {"orange", 255, 165, 0},
    {"pink", 255, 192, 203},  {"purple", 128, 0, 128},  {"brown", 165, 42, 42},
    {"gray", 128, 128, 128},  {"grey", 128, 128, 128},
};

static int hexdigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int clamp255(mp_obj_t o) {
    mp_int_t v = mp_obj_is_float(o) ? (mp_int_t)mp_obj_get_float(o) : mp_obj_get_int(o);
    return v < 0 ? 0 : v > 255 ? 255 : (int)v;
}

static uint16_t get_color(mp_obj_t o) {
    if (mp_obj_is_str(o)) {
        size_t len;
        const char *s = mp_obj_str_get_data(o, &len);
        if (len == 7 && s[0] == '#') {
            int v[6];
            for (int i = 0; i < 6; i++)
                if ((v[i] = hexdigit(s[1 + i])) < 0) goto bad;
            return RGB(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
        }
        for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
            if (strlen(NAMES[i].name) == len && memcmp(NAMES[i].name, s, len) == 0)
                return RGB(NAMES[i].r, NAMES[i].g, NAMES[i].b);
        goto bad;
    }
    if (mp_obj_is_type(o, &mp_type_tuple) || mp_obj_is_type(o, &mp_type_list)) {
        size_t n;
        mp_obj_t *items;
        mp_obj_get_array(o, &n, &items);
        if (n != 3) goto bad;
        return RGB(clamp255(items[0]), clamp255(items[1]), clamp255(items[2]));
    }
    if (mp_obj_is_int(o)) {                       /* a grey level, as on NumWorks */
        int v = clamp255(o);
        return RGB(v, v, v);
    }
bad:
    mp_raise_ValueError(MP_ERROR_TEXT("color: use (r, g, b), a name or '#rrggbb'"));
}

static mp_int_t arg_int(mp_obj_t o) { return mp_obj_get_int(o); }

static mp_obj_t k_fill_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    nwos_mp_display_used();
    display_fill_rect((int16_t)arg_int(args[0]), (int16_t)arg_int(args[1]),
                      (int16_t)arg_int(args[2]), (int16_t)arg_int(args[3]), get_color(args[4]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(k_fill_rect_obj, 5, 5, k_fill_rect);

static mp_obj_t k_set_pixel(mp_obj_t x, mp_obj_t y, mp_obj_t c) {
    nwos_mp_display_used();
    display_pixel((int16_t)arg_int(x), (int16_t)arg_int(y), get_color(c));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(k_set_pixel_obj, k_set_pixel);

static mp_obj_t rgb_tuple(uint16_t c) {
    /* RGB565 back to 8 bits per channel, the low bits copied from the high */
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    mp_obj_t t[3] = {
        MP_OBJ_NEW_SMALL_INT((r << 3) | (r >> 2)),
        MP_OBJ_NEW_SMALL_INT((g << 2) | (g >> 4)),
        MP_OBJ_NEW_SMALL_INT((b << 3) | (b >> 2)),
    };
    return mp_obj_new_tuple(3, t);
}

static mp_obj_t k_get_pixel(mp_obj_t x, mp_obj_t y) {
    return rgb_tuple(display_get_pixel((int16_t)arg_int(x), (int16_t)arg_int(y)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(k_get_pixel_obj, k_get_pixel);

static mp_obj_t k_draw_string(size_t n_args, const mp_obj_t *args) {
    nwos_mp_display_used();
    uint16_t fg = n_args > 3 ? get_color(args[3]) : BLACK;
    uint16_t bg = n_args > 4 ? get_color(args[4]) : WHITE;
    display_str((int16_t)arg_int(args[1]), (int16_t)arg_int(args[2]),
                mp_obj_str_get_str(args[0]), fg, bg);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(k_draw_string_obj, 3, 5, k_draw_string);

static mp_obj_t k_color(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) return rgb_tuple(get_color(args[0]));
    mp_obj_t t[3] = {
        MP_OBJ_NEW_SMALL_INT(clamp255(args[0])),
        MP_OBJ_NEW_SMALL_INT(clamp255(args[1])),
        MP_OBJ_NEW_SMALL_INT(clamp255(args[2])),
    };
    return mp_obj_new_tuple(3, t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(k_color_obj, 1, 3, k_color);

static const mp_rom_map_elem_t kandinsky_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_kandinsky) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect),   MP_ROM_PTR(&k_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pixel),   MP_ROM_PTR(&k_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_pixel),   MP_ROM_PTR(&k_get_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_string), MP_ROM_PTR(&k_draw_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_color),       MP_ROM_PTR(&k_color_obj) },
};
static MP_DEFINE_CONST_DICT(kandinsky_globals, kandinsky_globals_table);

const mp_obj_module_t nwos_kandinsky_module = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&kandinsky_globals,
};
MP_REGISTER_MODULE(MP_QSTR_kandinsky, nwos_kandinsky_module);
