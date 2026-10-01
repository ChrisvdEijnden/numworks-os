#pragma once
#include <stdbool.h>

/* Read one line from the keypad (or the debug UART), blocking, for
 * code that can't return to the event loop, such as Python's input().
 * buf holds the starting text. draw() shows the line being typed;
 * the screen is pushed to the LCD after each change. Returns false if
 * BACK or HOME (or Ctrl-C on the UART) cancels. */
bool line_input(char *buf, int max,
                void (*draw)(const char *text, bool shift, bool alpha));
