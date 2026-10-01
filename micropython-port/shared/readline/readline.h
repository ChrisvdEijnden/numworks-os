/* ================================================================
 * Stands in for MicroPython's shared/readline/readline.h, which the
 * embed port doesn't ship. Python's input() calls readline(); ours
 * (mp_port.c) reads a line from the calculator's keypad.
 * ================================================================ */
#pragma once
#include "py/misc.h"

#define CHAR_CTRL_C (3)
#define CHAR_CTRL_D (4)

int readline(vstr_t *line, const char *prompt);
