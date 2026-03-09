#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Subsystem init */
void hal_init(void);

/* Timing */
uint32_t hal_tick_ms(void);
void     hal_delay_ms(uint32_t ms);

/* Debug UART */
void hal_uart_putc(char c);
void hal_uart_puts(const char *s);
int  hal_uart_getc(void);

/* LED / status */
void hal_led_set(bool on);
