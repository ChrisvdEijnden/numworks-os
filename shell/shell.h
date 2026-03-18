#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../kernel/kernel.h"

void shell_init(void);
void shell_redraw(void);
void shell_handle_event(const kernel_event_t *ev);
void shell_puts(const char *s);
void shell_putc(char c);

/* Called by commands.c to print output */
void shell_print(const char *fmt, ...);
