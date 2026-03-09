#pragma once
#include <stdint.h>
void hal_uart_init(void);
void hal_uart_putc(char c);
void hal_uart_puts(const char *s);
int  hal_uart_getc(void);  /* -1 if empty */
int  hal_uart_available(void);
