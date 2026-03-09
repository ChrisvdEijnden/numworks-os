/* ================================================================
 * NumWorks OS — MicroPython Port
 * File: micropython-port/mp_port.c
 *
 * Wires MicroPython to:
 *   - Our flash filesystem (mp_import_stat, mp_reader)
 *   - Our display (via a minimal 'display' module)
 *   - UART stdout
 *
 * MicroPython heap is carved from main RAM (MP_HEAP_SIZE = 48 KB).
 *
 * Build notes:
 *   1. Clone MicroPython: git clone https://github.com/micropython/micropython
 *   2. Copy this file into ports/numworks/
 *   3. Build: make -C mpy-cross && make -C ports/numworks
 *
 * Code size target: MicroPython core ~90 KB flash, 48 KB RAM heap
 * ================================================================ */
#include "mp_port.h"
#include "../include/config.h"
#include "../hal/uart.h"
#include "../hal/display.h"
#include "../fs/flashfs.h"
#include <string.h>
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────
 * The actual MicroPython headers are from the MicroPython source
 * tree. Include them when building with MicroPython linked in.
 * These stubs allow the rest of our OS to compile standalone.
 * ───────────────────────────────────────────────────────────── */
#ifdef MICROPY_INCLUDED_PY_MPSTATE_H

#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "lib/utils/pyexec.h"

/* 48 KB heap in .bss */
static uint8_t mp_heap[MP_HEAP_SIZE];

void mp_init_port(void) {
    mp_stack_ctrl_init();
    mp_stack_set_limit(4096);      /* 4 KB Python call stack */
    gc_init(mp_heap, mp_heap + MP_HEAP_SIZE);
    mp_init();

    /* Register modules */
    extern const mp_obj_module_t mp_module_display;
    /* mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_)); */
}

int mp_exec_str(const char *src) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_string_gt_, src, strlen(src), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module = mp_compile(&pt, MP_QSTR__lt_module_gt_, false);
        mp_call_function_0(module);
        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        return -1;
    }
}

/* ── MicroPython platform hooks ──────────────────────────────── */
void mp_hal_stdout_tx_str(const char *str) {
    hal_uart_puts(str);
    /* Also write to shell output */
    extern void shell_puts(const char *);
    shell_puts(str);
}

mp_uint_t mp_hal_stdin_rx_chr(void) {
    int c;
    while ((c = hal_uart_getc()) < 0) { __asm volatile("wfi"); }
    return (mp_uint_t)c;
}

/* ── Filesystem import hook ──────────────────────────────────── */
mp_import_stat_t mp_import_stat(const char *path) {
    if (flashfs_exists(path)) return MP_IMPORT_STAT_FILE;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)kwargs;
    const char *fname = mp_obj_str_get_str(args[0]);
    /* Minimal: return a bytes object with the file contents */
    uint32_t off, sz;
    if (flashfs_open_read(fname, &off, &sz) < 0)
        mp_raise_OSError(MP_ENOENT);
    vstr_t vstr;
    vstr_init_len(&vstr, sz);
    flashfs_read(off, vstr.buf, sz);
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

/* ── display module ──────────────────────────────────────────── */
static mp_obj_t mp_display_fill(mp_obj_t col) {
    display_fill((uint16_t)mp_obj_get_int(col));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_display_fill_obj, mp_display_fill);

static mp_obj_t mp_display_str3(mp_obj_t x, mp_obj_t y, mp_obj_t s) {
    display_str(mp_obj_get_int(x), mp_obj_get_int(y),
                mp_obj_str_get_str(s), WHITE, BLACK);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(mp_display_str_obj, mp_display_str3);

static mp_obj_t mp_display_flush_obj_fn(void) {
    display_flush();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_display_flush_obj, mp_display_flush_obj_fn);

static const mp_rom_map_elem_t display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display) },
    { MP_ROM_QSTR(MP_QSTR_fill),     MP_ROM_PTR(&mp_display_fill_obj)  },
    { MP_ROM_QSTR(MP_QSTR_str),      MP_ROM_PTR(&mp_display_str_obj)   },
    { MP_ROM_QSTR(MP_QSTR_flush),    MP_ROM_PTR(&mp_display_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_BLACK),    MP_ROM_INT(BLACK) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),    MP_ROM_INT(WHITE) },
    { MP_ROM_QSTR(MP_QSTR_RED),      MP_ROM_INT(RED)   },
    { MP_ROM_QSTR(MP_QSTR_GREEN),    MP_ROM_INT(GREEN) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),     MP_ROM_INT(BLUE)  },
};
static MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);
const mp_obj_module_t mp_module_display = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_display, mp_module_display);

#else /* ── Stub when MicroPython not linked ───────────────────── */

void mp_init_port(void) {}

int mp_exec_str(const char *src) {
    (void)src;
    extern void shell_puts(const char *);
    shell_puts("[MicroPython not linked — see micropython-port/mp_port.c]\n");
    return -1;
}

#endif /* MICROPY_INCLUDED_PY_MPSTATE_H */
