/* ================================================================
 * NumWorks OS — MicroPython Port
 * File: micropython-port/mp_port.c
 *
 * Two build modes:
 *
 * A) WITH MicroPython (define MICROPY_INCLUDED_PY_MPSTATE_H by
 *    building with the MicroPython py/ tree on the include path):
 *    - Real mp_init / gc_init / lexer / compiler used
 *    - display module registered
 *
 * B) WITHOUT MicroPython (default standalone build):
 *    - Stub functions compile cleanly
 *    - mp_exec_capture writes a "Python not linked" message
 *    - Everything else in the OS still works
 * ================================================================ */
#include "mp_port.h"
#include "../include/config.h"
#include "../hal/display.h"
#include "../fs/flashfs.h"
#include "../include/string.h"

/* ── Output capture state (shared by both build modes) ─────────── */
static char *s_cap_buf = NULL;
static int   s_cap_max = 0;
static int   s_cap_len = 0;

static void cap_putc(char c) {
    if (s_cap_buf && s_cap_len < s_cap_max)
        s_cap_buf[s_cap_len++] = c;
}

static void cap_puts(const char *s) {
    while (*s) cap_putc(*s++);
}

/* ================================================================
 * MODE A — Full MicroPython build
 * ================================================================ */
#ifdef MICROPY_INCLUDED_PY_MPSTATE_H

#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "lib/utils/pyexec.h"

static uint8_t mp_heap[MP_HEAP_SIZE] __attribute__((section(".mp_heap")));

void mp_init_port(void) {
    mp_stack_ctrl_init();
    mp_stack_set_limit(4096);
    gc_init(mp_heap, mp_heap + MP_HEAP_SIZE);
    mp_init();
}

void mp_exec_str(const char *src) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_string_gt_, src, strlen(src), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module = mp_compile(&pt, MP_QSTR__lt_module_gt_, false);
        mp_call_function_0(module);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

void mp_hal_stdout_tx_str(const char *str) {
    cap_puts(str);
    extern void shell_puts(const char *);
    shell_puts(str);
}

mp_uint_t mp_hal_stdin_rx_chr(void) {
    /* Block waiting for UART character */
    extern int hal_uart_getc(void);
    int c;
    while ((c = hal_uart_getc()) < 0) { __asm volatile("wfi"); }
    return (mp_uint_t)c;
}

mp_import_stat_t mp_import_stat(const char *path) {
    if (flashfs_exists(path)) return MP_IMPORT_STAT_FILE;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)kwargs;
    const char *fname = mp_obj_str_get_str(args[0]);
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

static mp_obj_t mp_display_flush_fn(void) {
    display_flush();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_display_flush_obj, mp_display_flush_fn);

static const mp_rom_map_elem_t display_globals_table[] = {
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
static MP_DEFINE_CONST_DICT(display_globals, display_globals_table);
const mp_obj_module_t mp_module_display = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_globals,
};
MP_REGISTER_MODULE(MP_QSTR_display, mp_module_display);

void mp_deinit_port(void) { mp_deinit(); }

#else
/* ================================================================
 * MODE B — Stub build (no MicroPython source tree available)
 * The OS compiles and links; Python apps show a "not available" msg.
 * ================================================================ */

void mp_init_port(void) {}
void mp_deinit_port(void) {}

void mp_exec_str(const char *src) {
    (void)src;
    /* Route "Python not linked" message to capture buffer / shell */
    cap_puts("Python niet beschikbaar.\n"
             "Voeg MicroPython broncode toe en herbouw.\n");
    extern void shell_puts(const char *);
    shell_puts("Python niet beschikbaar.\n");
}

#endif /* MICROPY_INCLUDED_PY_MPSTATE_H */

/* ================================================================
 * Output capture — used by calculator, functions, equations apps
 * Works in both build modes.
 * ================================================================ */
int mp_exec_capture(const char *code, char *out, int outlen) {
    if (!out || outlen < 1) return -1;
    s_cap_buf = out;
    s_cap_max = outlen - 1;
    s_cap_len = 0;
    out[0]    = 0;
    mp_exec_str(code);
    out[s_cap_len] = 0;
    s_cap_buf = NULL;
    return s_cap_len;
}
