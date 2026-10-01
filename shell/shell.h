#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../kernel/kernel.h"

void shell_init(void);
void shell_redraw(void);
void shell_handle_event(const kernel_event_t *ev);
void shell_tick(void);     /* serial input; called every loop while shown */
void shell_puts(const char *s);
void shell_putc(char c);

/* Python's console when it runs from the shell (see mp_port.h) */
void shell_show(void);
bool shell_read_line(char *buf, int max);

/* Called by commands.c to print output */
void shell_print(const char *fmt, ...);
