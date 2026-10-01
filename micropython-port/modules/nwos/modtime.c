/* ================================================================
 * NumWorks OS — `time` module for MicroPython
 *
 * There is no real-time clock, so time() and monotonic() count from
 * power-on. Sleeping keeps the screen up to date and can be
 * interrupted with BACK.
 * ================================================================ */
#include "py/runtime.h"
#include "py/smallint.h"
#include "../../mp_port.h"

extern uint32_t hal_tick_ms(void);
extern uint32_t hal_tick_us(void);

/* ticks_*() wrap within MicroPython's small-int range, like on other ports */
#define TICKS_PERIOD (MP_SMALL_INT_POSITIVE_MASK + 1)
#define TICKS_MAX    (TICKS_PERIOD - 1)
#define TICKS_HALF   (TICKS_PERIOD / 2)

static mp_obj_t time_sleep(mp_obj_t seconds) {
    mp_float_t s = mp_obj_get_float(seconds);
    if (s > 0) nwos_mp_wait_ms((uint32_t)(s * 1000 + (mp_float_t)0.5));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(time_sleep_obj, time_sleep);

static mp_obj_t time_sleep_ms(mp_obj_t ms) {
    mp_int_t v = mp_obj_get_int(ms);
    if (v > 0) nwos_mp_wait_ms((uint32_t)v);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(time_sleep_ms_obj, time_sleep_ms);

static mp_obj_t time_sleep_us(mp_obj_t us) {
    mp_int_t v = mp_obj_get_int(us);
    if (v >= 1000) {
        nwos_mp_wait_ms((uint32_t)(v / 1000));
        v %= 1000;
    }
    if (v > 0) {
        uint32_t start = hal_tick_us();
        while ((uint32_t)(hal_tick_us() - start) < (uint32_t)v) {}
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(time_sleep_us_obj, time_sleep_us);

static mp_obj_t time_ticks_ms(void) {
    return MP_OBJ_NEW_SMALL_INT(hal_tick_ms() & TICKS_MAX);
}
static MP_DEFINE_CONST_FUN_OBJ_0(time_ticks_ms_obj, time_ticks_ms);

static mp_obj_t time_ticks_us(void) {
    return MP_OBJ_NEW_SMALL_INT(hal_tick_us() & TICKS_MAX);
}
static MP_DEFINE_CONST_FUN_OBJ_0(time_ticks_us_obj, time_ticks_us);

static mp_obj_t time_ticks_diff(mp_obj_t end, mp_obj_t start) {
    mp_uint_t d = ((mp_uint_t)mp_obj_get_int(end) - (mp_uint_t)mp_obj_get_int(start) + TICKS_HALF) & TICKS_MAX;
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)d - TICKS_HALF);
}
static MP_DEFINE_CONST_FUN_OBJ_2(time_ticks_diff_obj, time_ticks_diff);

static mp_obj_t time_ticks_add(mp_obj_t ticks, mp_obj_t delta) {
    mp_int_t d = mp_obj_get_int(delta);
    if (d < -(mp_int_t)TICKS_HALF || d >= (mp_int_t)TICKS_HALF)
        mp_raise_msg(&mp_type_OverflowError, MP_ERROR_TEXT("ticks interval overflow"));
    return MP_OBJ_NEW_SMALL_INT(((mp_uint_t)mp_obj_get_int(ticks) + (mp_uint_t)d) & TICKS_MAX);
}
static MP_DEFINE_CONST_FUN_OBJ_2(time_ticks_add_obj, time_ticks_add);

/* Whole seconds since power-on */
static mp_obj_t time_time(void) {
    return mp_obj_new_int_from_uint(hal_tick_ms() / 1000U);
}
static MP_DEFINE_CONST_FUN_OBJ_0(time_time_obj, time_time);

/* Seconds since power-on, as a float (millisecond resolution) */
static mp_obj_t time_monotonic(void) {
    return mp_obj_new_float((mp_float_t)hal_tick_ms() / 1000);
}
static MP_DEFINE_CONST_FUN_OBJ_0(time_monotonic_obj, time_monotonic);

static const mp_rom_map_elem_t time_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_ROM_QSTR(MP_QSTR_time) },
    { MP_ROM_QSTR(MP_QSTR_sleep),      MP_ROM_PTR(&time_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms),   MP_ROM_PTR(&time_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_us),   MP_ROM_PTR(&time_sleep_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms),   MP_ROM_PTR(&time_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_us),   MP_ROM_PTR(&time_ticks_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_diff), MP_ROM_PTR(&time_ticks_diff_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_add),  MP_ROM_PTR(&time_ticks_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_time),       MP_ROM_PTR(&time_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_monotonic),  MP_ROM_PTR(&time_monotonic_obj) },
};
static MP_DEFINE_CONST_DICT(time_globals, time_globals_table);

const mp_obj_module_t nwos_time_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&time_globals,
};
MP_REGISTER_MODULE(MP_QSTR_time, nwos_time_module);
