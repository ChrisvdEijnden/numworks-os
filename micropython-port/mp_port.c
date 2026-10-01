/* ================================================================
 * NumWorks OS — MicroPython Port
 * File: micropython-port/mp_port.c
 *
 * With MicroPython: `make mp` generates micropython-port/micropython_embed
 * from a MicroPython checkout; the Makefile then compiles it in and
 * defines NWOS_MICROPYTHON. Without it, a stub reports that Python is
 * not available and everything else still works.
 *
 * Output goes to a console (mp_console_t) set by the app running
 * Python; by default that is the shell. It is put on the screen while
 * a script runs, at most every SHOW_MS.
 * ================================================================ */
#include <stdbool.h>
#include <string.h>
#include "mp_port.h"
#include "../hal/hal.h"
#include "../hal/keyboard.h"
#include "../hal/display.h"
#include "../fs/flashfs.h"

/* ── Consoles ─────────────────────────────────────────────────── */
extern void shell_puts(const char *s);
extern void shell_show(void);
extern bool shell_read_line(char *buf, int max);

static void shell_write(const char *s, size_t n) {
    char chunk[64];
    while (n) {
        size_t k = n < sizeof(chunk) - 1 ? n : sizeof(chunk) - 1;
        memcpy(chunk, s, k);
        chunk[k] = 0;
        shell_puts(chunk);
        s += k; n -= k;
    }
}

static const mp_console_t s_shell_console = { shell_write, shell_read_line, shell_show };
static const mp_console_t *s_console = &s_shell_console;

void mp_set_console(const mp_console_t *console) {
    s_console = console ? console : &s_shell_console;
}

/* ── Live output ──────────────────────────────────────────────── */
#define SHOW_MS 50
static bool     s_pending;    /* output not yet on the screen */
static bool     s_graphics;   /* the script drew with `display` */
static uint32_t s_last_show;

static void show_output(bool force) {
    if (!s_pending || s_graphics) return;
    uint32_t now = hal_tick_ms();
    if (!force && now - s_last_show < SHOW_MS) return;
    s_last_show = now;
    s_pending = false;
    s_console->show();
}

static void out_strn(const char *s, size_t n) {
    s_console->write(s, n);
    s_pending = true;
    show_output(false);
}

static void begin_run(void) {
    s_graphics  = false;
    s_pending   = false;
    s_last_show = hal_tick_ms() - SHOW_MS;    /* first output shows at once */
}

void mp_pause_after_graphics(void) {
    if (!s_graphics) return;
    s_graphics = false;
    display_fill_rect(0, LCD_HEIGHT - 12, LCD_WIDTH, 12, BLACK);
    display_str(4, LCD_HEIGHT - 10, "Druk op een toets om verder te gaan", GREY, BLACK);
    display_flush();
    key_event_t ev;
    for (;;) {
        hal_delay_ms(5);
        if (keyboard_poll(&ev) && ev.action == 0) return;
    }
}

#ifdef NWOS_MICROPYTHON
/* ================================================================
 * With MicroPython (embed port)
 * ================================================================ */
#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/repl.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "py/mphal.h"
#include "port/micropython_embed.h"
#include "shared/readline/readline.h"

/* Heap and stack regions from the linker script */
extern uint8_t _smp_heap[], _emp_heap[];
extern uint8_t _sstack[], _estack[];

#define STACK_MARGIN 2048   /* headroom for C code below the Python VM */

void mp_init_port(void) {
    mp_embed_init(_smp_heap, (size_t)(_emp_heap - _smp_heap), _estack);
    mp_stack_set_limit((mp_uint_t)(_estack - _sstack) - STACK_MARGIN);
}

void mp_deinit_port(void) { mp_embed_deinit(); }

bool mp_heap_stats(uint32_t *used, uint32_t *total) {
    gc_info_t info;
    gc_info(&info);
    *used  = (uint32_t)info.used;
    *total = (uint32_t)info.total;
    return true;
}

/* MicroPython's stdout */
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    out_strn(str, len);
}

/* An exception with no handler at all (linked in with --wrap: the
 * embed port's own version just hangs) */
void __wrap_nlr_jump_fail(void *val) {
    (void)val;
    hal_panic("MicroPython: onopgevangen fout");
}

/* micropython.kbd_intr(): the interrupt key is BACK/HOME, not a char */
void mp_hal_set_interrupt_char(int c) { (void)c; }

/* Called from the VM loop (see mpconfigport.h): BACK or HOME raises
 * KeyboardInterrupt, and new output reaches the screen. Keys pressed
 * while a script runs are consumed. */
void nwos_mp_poll(void) {
    static uint32_t last;
    hal_watchdog_feed();          /* a long computation isn't a hang */
    uint32_t now = hal_tick_ms();
    if (now - last < 20) return;
    last = now;
    key_event_t ev;
    while (keyboard_poll(&ev)) {
        if (ev.action == 0 && (ev.key == KEY_BACK || ev.key == KEY_HOME))
            mp_sched_keyboard_interrupt();
    }
    show_output(false);
}

/* time.sleep() & co: wait in WFI, but keep BACK and the screen alive */
void nwos_mp_wait_ms(uint32_t ms) {
    show_output(true);
    uint32_t start = hal_tick_ms();
    while (hal_tick_ms() - start < ms) {
        nwos_mp_poll();
        mp_handle_pending(true);      /* raises a pending KeyboardInterrupt */
        hal_delay_ms(1);
    }
}

void nwos_mp_display_used(void) { s_graphics = true; }

/* input(): a line from the console. The line is echoed into the output,
 * as a terminal would. */
int readline(vstr_t *line, const char *prompt) {
    if (prompt && *prompt) out_strn(prompt, strlen(prompt));
    show_output(true);
    char buf[128];
    buf[0] = 0;
    bool ok = s_console->read_line(buf, (int)sizeof(buf));
    if (ok) out_strn(buf, strlen(buf));
    out_strn("\n", 1);
    if (!ok) return CHAR_CTRL_C;
    vstr_add_str(line, buf);
    return 0;
}

/* ── import: modules are .py files in the flash file system ────── */
mp_import_stat_t mp_import_stat(const char *path) {
    /* The file system is flat: no packages (directories) */
    if (strchr(path, '/')) return MP_IMPORT_STAT_NO_EXIST;
    return flashfs_exists(path) ? MP_IMPORT_STAT_FILE : MP_IMPORT_STAT_NO_EXIST;
}

/* The source is read straight from memory-mapped flash: parsing
 * finishes before the module runs, so nothing can move it meanwhile. */
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *src;
    uint32_t size;
    if (!flashfs_map(qstr_str(filename), &src, &size)) mp_raise_OSError(MP_ENOENT);
    return mp_lexer_new_from_str_len(filename, src, size, 0);
}

/* Forget imported modules, so the next import reads the file again */
void mp_forget_imports(void) {
    mp_map_t *mods = &MP_STATE_VM(mp_loaded_modules_dict).map;
    for (size_t i = 0; i < mods->alloc; i++) {
        if (mp_map_slot_is_filled(mods, i))
            mp_map_lookup(mods, mods->table[i].key, MP_MAP_LOOKUP_REMOVE_IF_FOUND);
    }
}

/* ── Running code ─────────────────────────────────────────────── */
static void exec_source(qstr name, const char *src, size_t len, mp_parse_input_kind_t kind) {
    begin_run();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(name, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, kind);
        mp_obj_t fun = mp_compile(&tree, source_name, kind == MP_PARSE_SINGLE_INPUT);
        mp_call_function_0(fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

extern int nwos_close_files(void);

int mp_close_files(void) {
    int failed = nwos_close_files();
    if (failed) {
        static const char msg[] = "Let op: een geopend bestand kon niet worden opgeslagen.\n";
        out_strn(msg, sizeof(msg) - 1);
    }
    return failed;
}

void mp_exec_str(const char *src) {
    exec_source(MP_QSTR__lt_stdin_gt_, src, strlen(src), MP_PARSE_FILE_INPUT);
    mp_close_files();
}

bool mp_exec_file(const char *name) {
    const char *src;
    uint32_t size;
    if (!flashfs_map(name, &src, &size)) return false;
    mp_forget_imports();                  /* a run sees edited modules */
    exec_source(qstr_from_str(name), src, size, MP_PARSE_FILE_INPUT);
    mp_close_files();
    return true;
}

extern int nwos_flush_files(void);

void mp_exec_repl(const char *src) {
    exec_source(MP_QSTR__lt_stdin_gt_, src, strlen(src), MP_PARSE_SINGLE_INPUT);
    if (nwos_flush_files()) {
        static const char msg[] = "Let op: een geopend bestand kon niet worden opgeslagen.\n";
        out_strn(msg, sizeof(msg) - 1);
    }
}

bool mp_repl_incomplete(const char *src) {
    return mp_repl_continue_with_input(src);
}

#else
/* ================================================================
 * Stub build (no MicroPython): Python apps show a "not available" msg
 * ================================================================ */
void mp_init_port(void) {}
void mp_deinit_port(void) {}
bool mp_heap_stats(uint32_t *used, uint32_t *total) { (void)used; (void)total; return false; }
void mp_forget_imports(void) {}
int mp_close_files(void) { return 0; }

static void not_available(void) {
    static const char msg[] = "Python niet beschikbaar (bouw met 'make mp').\n";
    begin_run();
    out_strn(msg, sizeof(msg) - 1);
}

void mp_exec_str(const char *src) { (void)src; not_available(); }
bool mp_exec_file(const char *name) {
    if (!flashfs_exists(name)) return false;
    not_available();
    return true;
}
void mp_exec_repl(const char *src) { (void)src; not_available(); }
bool mp_repl_incomplete(const char *src) { (void)src; return false; }

#endif /* NWOS_MICROPYTHON */
