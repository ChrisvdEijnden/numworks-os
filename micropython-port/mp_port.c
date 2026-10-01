/* ================================================================
 * NumWorks OS — MicroPython Port
 * File: micropython-port/mp_port.c
 *
 * With MicroPython: `make mp` generates micropython-port/micropython_embed
 * from a MicroPython checkout; the Makefile then compiles it in and
 * defines NWOS_MICROPYTHON. Without it, a stub reports that Python is
 * not available and everything else still works.
 * ================================================================ */
#include <stdbool.h>
#include <string.h>
#include "mp_port.h"
#include "../hal/hal.h"
#include "../hal/keyboard.h"

extern void shell_puts(const char *s);

/* ── Output: into a capture buffer (apps) or the shell (scripts) ── */
static char *s_cap_buf = NULL;
static int   s_cap_max = 0;
static int   s_cap_len = 0;

static void out_strn(const char *s, size_t n) {
    if (s_cap_buf) {
        while (n-- && s_cap_len < s_cap_max) s_cap_buf[s_cap_len++] = *s++;
        return;
    }
    char chunk[64];
    while (n) {
        size_t k = n < sizeof(chunk) - 1 ? n : sizeof(chunk) - 1;
        memcpy(chunk, s, k);
        chunk[k] = 0;
        shell_puts(chunk);
        s += k; n -= k;
    }
}

#ifdef NWOS_MICROPYTHON
/* ================================================================
 * With MicroPython (embed port)
 * ================================================================ */
#include "py/compile.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "py/mphal.h"
#include "port/micropython_embed.h"

/* Heap and stack regions from the linker script */
extern uint8_t _smp_heap[], _emp_heap[];
extern uint8_t _sstack[], _estack[];

#define STACK_MARGIN 2048   /* headroom for C code below the Python VM */

void mp_init_port(void) {
    mp_embed_init(_smp_heap, (size_t)(_emp_heap - _smp_heap), _estack);
    mp_stack_set_limit((mp_uint_t)(_estack - _sstack) - STACK_MARGIN);
}

void mp_deinit_port(void) { mp_embed_deinit(); }

/* MicroPython's stdout */
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    out_strn(str, len);
}

/* micropython.kbd_intr(): the interrupt key is BACK/HOME, not a char */
void mp_hal_set_interrupt_char(int c) { (void)c; }

/* Called from the VM loop (see mpconfigport.h): BACK or HOME raises
 * KeyboardInterrupt. Keys pressed while a script runs are consumed. */
void nwos_mp_poll(void) {
    static uint32_t last;
    uint32_t now = hal_tick_ms();
    if (now - last < 20) return;
    last = now;
    key_event_t ev;
    while (keyboard_poll(&ev)) {
        if (ev.action == 0 && (ev.key == KEY_BACK || ev.key == KEY_HOME))
            mp_sched_keyboard_interrupt();
    }
}

static void exec(const char *src, mp_parse_input_kind_t kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, kind);
        mp_obj_t fun = mp_compile(&tree, source_name, kind == MP_PARSE_SINGLE_INPUT);
        mp_call_function_0(fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

void mp_exec_str(const char *src) { exec(src, MP_PARSE_FILE_INPUT); }
static void exec_repl(const char *src) { exec(src, MP_PARSE_SINGLE_INPUT); }

#else
/* ================================================================
 * Stub build (no MicroPython): Python apps show a "not available" msg
 * ================================================================ */
void mp_init_port(void) {}
void mp_deinit_port(void) {}

void mp_exec_str(const char *src) {
    (void)src;
    static const char msg[] = "Python niet beschikbaar (bouw met 'make mp').\n";
    out_strn(msg, sizeof(msg) - 1);
}
static void exec_repl(const char *src) { mp_exec_str(src); }

#endif /* NWOS_MICROPYTHON */

/* ================================================================
 * Output capture — used by the Python REPL app. Works in both builds.
 * ================================================================ */
static int capture(void (*run)(const char *), const char *code, char *out, int outlen) {
    if (!out || outlen < 1) return -1;
    s_cap_buf = out;
    s_cap_max = outlen - 1;
    s_cap_len = 0;
    run(code);
    out[s_cap_len] = 0;
    s_cap_buf = NULL;
    return s_cap_len;
}

int mp_exec_capture(const char *code, char *out, int outlen) {
    return capture(mp_exec_str, code, out, outlen);
}

int mp_exec_repl_capture(const char *code, char *out, int outlen) {
    return capture(exec_repl, code, out, outlen);
}
