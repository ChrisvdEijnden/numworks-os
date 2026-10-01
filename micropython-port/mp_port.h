/* ================================================================
 * NumWorks OS — MicroPython Platform Port
 * File: micropython-port/mp_port.h
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Where Python's output goes and where input() reads from. The app
 * that runs Python sets one; without one (NULL) it is the shell. */
typedef struct {
    void (*write)(const char *s, size_t n);
    /* Read a line for input() into buf (at most max-1 characters);
     * false if the user cancelled (BACK/HOME) */
    bool (*read_line)(char *buf, int max);
    /* Draw the output so far and push it to the LCD, so prints show
     * up while a script is still running */
    void (*show)(void);
} mp_console_t;

void mp_init_port(void);
void mp_deinit_port(void);
void mp_set_console(const mp_console_t *console);

/* Run a script: a string, or a .py file from flash (false: not found) */
void mp_exec_str(const char *code);
bool mp_exec_file(const char *name);

/* One REPL entry, which may span lines: an expression's value is
 * printed, as at the >>> prompt */
void mp_exec_repl(const char *code);

/* Save and close files opened for writing that are still open (done
 * after every script; the Python app does it when it is left).
 * Returns how many couldn't be saved. */
int mp_close_files(void);

/* Forget imported modules, so the next import reads the file again
 * (mp_exec_file does this before each run) */
void mp_forget_imports(void);

/* Whether a REPL entry needs more lines (an open block, bracket or
 * triple quote). An empty last line ends a block. */
bool mp_repl_incomplete(const char *code);

/* After a run that drew with the `display` module: keep the drawing
 * on screen until a key is pressed (returns at once otherwise) */
void mp_pause_after_graphics(void);

/* Python heap use; false if MicroPython isn't built in */
bool mp_heap_stats(uint32_t *used, uint32_t *total);

/* For the modules: wait while keeping the screen up to date and BACK
 * working; the display module reports drawing so the console stays
 * off the screen until the script ends */
void nwos_mp_wait_ms(uint32_t ms);
void nwos_mp_display_used(void);
