/* ================================================================
 * NumWorks OS — `ion` module for MicroPython
 *
 * The keyboard module of NumWorks' own Python:
 *   ion.keydown(ion.KEY_OK) -> True while the key is held
 * Key numbers are NumWorks' (row * 6 + column). BACK also stops the
 * script, as on the stock firmware.
 * ================================================================ */
#include "py/runtime.h"
#include "../../../hal/keyboard.h"
#include "../../mp_port.h"

static mp_obj_t ion_keydown(mp_obj_t k) {
    nwos_mp_poll();                    /* fresh key state; BACK interrupts */
    mp_int_t n = mp_obj_get_int(k);
    if (n < 0) return mp_const_false;
    key_code_t key = keyboard_key_at((unsigned)n);
    return mp_obj_new_bool(key != KEY_NONE && keyboard_is_pressed(key));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ion_keydown_obj, ion_keydown);

#define K(name, n) { MP_ROM_QSTR(MP_QSTR_KEY_##name), MP_ROM_INT(n) }
static const mp_rom_map_elem_t ion_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ion) },
    { MP_ROM_QSTR(MP_QSTR_keydown),  MP_ROM_PTR(&ion_keydown_obj) },
    K(LEFT, 0), K(UP, 1), K(DOWN, 2), K(RIGHT, 3), K(OK, 4), K(BACK, 5),
    K(HOME, 6), K(ONOFF, 8),
    K(SHIFT, 12), K(ALPHA, 13), K(XNT, 14), K(VAR, 15), K(TOOLBOX, 16), K(BACKSPACE, 17),
    K(EXP, 18), K(LN, 19), K(LOG, 20), K(IMAGINARY, 21), K(COMMA, 22), K(POWER, 23),
    K(SINE, 24), K(COSINE, 25), K(TANGENT, 26), K(PI, 27), K(SQRT, 28), K(SQUARE, 29),
    K(SEVEN, 30), K(EIGHT, 31), K(NINE, 32), K(LEFTPARENTHESIS, 33), K(RIGHTPARENTHESIS, 34),
    K(FOUR, 36), K(FIVE, 37), K(SIX, 38), K(MULTIPLICATION, 39), K(DIVISION, 40),
    K(ONE, 42), K(TWO, 43), K(THREE, 44), K(PLUS, 45), K(MINUS, 46),
    K(ZERO, 48), K(DOT, 49), K(EE, 50), K(ANS, 51), K(EXE, 52),
};
static MP_DEFINE_CONST_DICT(ion_globals, ion_globals_table);

const mp_obj_module_t nwos_ion_module = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ion_globals,
};
MP_REGISTER_MODULE(MP_QSTR_ion, nwos_ion_module);
