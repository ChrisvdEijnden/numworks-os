/* ================================================================
 * NumWorks OS — MicroPython Platform Port
 * File: micropython-port/mp_port.h
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stddef.h>

void mp_init_port(void);

/* Run a script (file input); output goes to the shell */
void mp_exec_str(const char *code);

/**
 * Execute Python code and capture stdout into `out` (null-terminated).
 * Returns number of characters captured, or -1 on error.
 */
int  mp_exec_capture(const char *code, char *out, int outlen);

/* Like mp_exec_capture, but as one REPL line: an expression's value is
 * printed, as at the >>> prompt */
int  mp_exec_repl_capture(const char *code, char *out, int outlen);

void mp_deinit_port(void);
