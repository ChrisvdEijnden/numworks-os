/* ================================================================
 * NumWorks OS — `display` module for MicroPython
 *
 *   display.fill(colour)
 *   display.str(x, y, text[, fg[, bg]])      fg/bg default WHITE/BLACK
 *   display.pixel(x, y, colour)
 *   display.fill_rect(x, y, w, h, colour)
 *   display.rgb(r, g, b) -> colour
 *   display.flush()                          show what was drawn
 *
 * Once a script draws, the console stays off the screen until the
 * script ends, and the drawing stays up until a key is pressed.
 *   display.BLACK WHITE RED GREEN BLUE YELLOW CYAN GREY
 * ================================================================ */
#include "py/runtime.h"
#include "../../../hal/display.h"
#include "../../mp_port.h"

static mp_int_t arg_int(mp_obj_t o) { return mp_obj_get_int(o); }

static mp_obj_t py_fill(mp_obj_t colour) {
    nwos_mp_display_used();
    display_fill((uint16_t)arg_int(colour));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_fill_obj, py_fill);

static mp_obj_t py_str(size_t n_args, const mp_obj_t *args) {
    nwos_mp_display_used();
    uint16_t fg = n_args > 3 ? (uint16_t)arg_int(args[3]) : WHITE;
    uint16_t bg = n_args > 4 ? (uint16_t)arg_int(args[4]) : BLACK;
    display_str((int16_t)arg_int(args[0]), (int16_t)arg_int(args[1]),
                mp_obj_str_get_str(args[2]), fg, bg);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_str_obj, 3, 5, py_str);

static mp_obj_t py_pixel(mp_obj_t x, mp_obj_t y, mp_obj_t colour) {
    nwos_mp_display_used();
    display_pixel((int16_t)arg_int(x), (int16_t)arg_int(y), (uint16_t)arg_int(colour));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_pixel_obj, py_pixel);

static mp_obj_t py_fill_rect(size_t n_args, const mp_obj_t *args) {
    nwos_mp_display_used();
    (void)n_args;
    display_fill_rect((int16_t)arg_int(args[0]), (int16_t)arg_int(args[1]),
                      (int16_t)arg_int(args[2]), (int16_t)arg_int(args[3]),
                      (uint16_t)arg_int(args[4]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_fill_rect_obj, 5, 5, py_fill_rect);

static mp_obj_t py_rgb(mp_obj_t r, mp_obj_t g, mp_obj_t b) {
    return MP_OBJ_NEW_SMALL_INT(RGB(arg_int(r), arg_int(g), arg_int(b)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_rgb_obj, py_rgb);

static mp_obj_t py_flush(void) {
    nwos_mp_display_used();
    display_flush();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_flush_obj, py_flush);

static const mp_rom_map_elem_t display_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),  MP_ROM_QSTR(MP_QSTR_display) },
    { MP_ROM_QSTR(MP_QSTR_fill),      MP_ROM_PTR(&py_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_str),       MP_ROM_PTR(&py_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel),     MP_ROM_PTR(&py_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect), MP_ROM_PTR(&py_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_rgb),       MP_ROM_PTR(&py_rgb_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush),     MP_ROM_PTR(&py_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_BLACK),     MP_ROM_INT(BLACK) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),     MP_ROM_INT(WHITE) },
    { MP_ROM_QSTR(MP_QSTR_RED),       MP_ROM_INT(RED) },
    { MP_ROM_QSTR(MP_QSTR_GREEN),     MP_ROM_INT(GREEN) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),      MP_ROM_INT(BLUE) },
    { MP_ROM_QSTR(MP_QSTR_YELLOW),    MP_ROM_INT(YELLOW) },
    { MP_ROM_QSTR(MP_QSTR_CYAN),      MP_ROM_INT(CYAN) },
    { MP_ROM_QSTR(MP_QSTR_GREY),      MP_ROM_INT(GREY) },
};
static MP_DEFINE_CONST_DICT(display_globals, display_globals_table);

const mp_obj_module_t nwos_display_module = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_globals,
};
MP_REGISTER_MODULE(MP_QSTR_display, nwos_display_module);
