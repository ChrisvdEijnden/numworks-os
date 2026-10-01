/* ================================================================
 * NumWorks OS — `random` module for MicroPython
 *
 * Adapted from MicroPython's extmod/modrandom.c (MIT license,
 * Copyright (c) 2016 Paul Sokolovsky), which the embed port doesn't
 * ship. Generator: Yasmarang by Ilya Levin (public domain).
 *
 * Seeded from the time of the first call (key presses make that
 * unpredictable), unless seed() is called first.
 * ================================================================ */
#include "py/runtime.h"
#include "../../mp_port.h"

extern uint32_t hal_tick_us(void);

static uint32_t pad = 0xeda4baba, n_ = 69, d_ = 233;
static uint8_t  dat = 0;
static bool     seeded = false;

static void set_seed(uint32_t seed) {
    pad = seed; n_ = 69; d_ = 233; dat = 0;
    seeded = true;
}

static uint32_t yasmarang(void) {
    if (!seeded) set_seed(hal_tick_us() * 2654435761U);
    pad += dat + d_ * n_;
    pad = (pad << 3) + (pad >> 29);
    n_ = pad | 2;
    d_ ^= (pad << 31) + (pad >> 1);
    dat ^= (char)pad ^ (d_ >> 8) ^ 1;
    return pad ^ (d_ << 5) ^ (pad >> 18) ^ (dat << 1);
}

/* Uniform in [0, n), n > 0 */
static uint32_t randbelow(uint32_t n) {
    uint32_t mask = 1;
    while ((n & mask) < n) mask = (mask << 1) | 1;
    uint32_t r;
    do { r = yasmarang() & mask; } while (r >= n);
    return r;
}

/* Uniform in [0, 1) */
static mp_float_t randfloat(void) {
    mp_float_union_t u;
    u.p.sgn = 0;
    u.p.exp = (1 << (MP_FLOAT_EXP_BITS - 1)) - 1;
    u.p.frc = yasmarang();          /* the bit-field keeps the low bits */
    return u.f - 1;
}

static mp_obj_t random_getrandbits(mp_obj_t bits) {
    mp_int_t n = mp_obj_get_int(bits);
    if (n > 32 || n < 0) mp_raise_ValueError(MP_ERROR_TEXT("bits must be 32 or less"));
    if (n == 0) return MP_OBJ_NEW_SMALL_INT(0);
    return mp_obj_new_int_from_uint(yasmarang() & (0xFFFFFFFFU >> (32 - n)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(random_getrandbits_obj, random_getrandbits);

static mp_obj_t random_seed(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0 || args[0] == mp_const_none) set_seed(hal_tick_us() * 2654435761U);
    else set_seed((uint32_t)mp_obj_get_int_truncated(args[0]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(random_seed_obj, 0, 1, random_seed);

static mp_obj_t random_randrange(size_t n_args, const mp_obj_t *args) {
    mp_int_t start = mp_obj_get_int(args[0]);
    if (n_args == 1) {
        if (start > 0) return mp_obj_new_int(randbelow((uint32_t)start));
    } else {
        mp_int_t stop = mp_obj_get_int(args[1]);
        if (n_args == 2) {
            if (start < stop) return mp_obj_new_int(start + randbelow((uint32_t)(stop - start)));
        } else {
            mp_int_t step = mp_obj_get_int(args[2]), n = 0;
            if (step > 0)      n = (stop - start + step - 1) / step;
            else if (step < 0) n = (stop - start + step + 1) / step;
            if (n > 0) return mp_obj_new_int(start + step * (mp_int_t)randbelow((uint32_t)n));
        }
    }
    mp_raise_ValueError(MP_ERROR_TEXT("empty range"));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(random_randrange_obj, 1, 3, random_randrange);

static mp_obj_t random_randint(mp_obj_t a_in, mp_obj_t b_in) {
    mp_int_t a = mp_obj_get_int(a_in), b = mp_obj_get_int(b_in);
    if (a > b) mp_raise_ValueError(MP_ERROR_TEXT("empty range"));
    return mp_obj_new_int(a + (mp_int_t)randbelow((uint32_t)(b - a + 1)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(random_randint_obj, random_randint);

static mp_obj_t random_choice(mp_obj_t seq) {
    mp_int_t len = mp_obj_get_int(mp_obj_len(seq));
    if (len <= 0) mp_raise_type(&mp_type_IndexError);
    return mp_obj_subscr(seq, mp_obj_new_int(randbelow((uint32_t)len)), MP_OBJ_SENTINEL);
}
static MP_DEFINE_CONST_FUN_OBJ_1(random_choice_obj, random_choice);

static mp_obj_t random_random(void) {
    return mp_obj_new_float(randfloat());
}
static MP_DEFINE_CONST_FUN_OBJ_0(random_random_obj, random_random);

static mp_obj_t random_uniform(mp_obj_t a_in, mp_obj_t b_in) {
    mp_float_t a = mp_obj_get_float(a_in), b = mp_obj_get_float(b_in);
    return mp_obj_new_float(a + (b - a) * randfloat());
}
static MP_DEFINE_CONST_FUN_OBJ_2(random_uniform_obj, random_uniform);

static const mp_rom_map_elem_t random_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_random) },
    { MP_ROM_QSTR(MP_QSTR_getrandbits), MP_ROM_PTR(&random_getrandbits_obj) },
    { MP_ROM_QSTR(MP_QSTR_seed),        MP_ROM_PTR(&random_seed_obj) },
    { MP_ROM_QSTR(MP_QSTR_randrange),   MP_ROM_PTR(&random_randrange_obj) },
    { MP_ROM_QSTR(MP_QSTR_randint),     MP_ROM_PTR(&random_randint_obj) },
    { MP_ROM_QSTR(MP_QSTR_choice),      MP_ROM_PTR(&random_choice_obj) },
    { MP_ROM_QSTR(MP_QSTR_random),      MP_ROM_PTR(&random_random_obj) },
    { MP_ROM_QSTR(MP_QSTR_uniform),     MP_ROM_PTR(&random_uniform_obj) },
};
static MP_DEFINE_CONST_DICT(random_globals, random_globals_table);

const mp_obj_module_t nwos_random_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&random_globals,
};
MP_REGISTER_MODULE(MP_QSTR_random, nwos_random_module);
