/* only key_to_char is needed from the keyboard driver */
#include "../../include/stm32f730.h"
uint32_t hal_tick_ms(void); void hal_delay_ms(uint32_t); void hal_uart_puts(const char *);
#include "../../hal/keyboard.c"
uint32_t hal_tick_ms(void) { return 0; } void hal_delay_ms(uint32_t m) { (void)m; } void hal_uart_puts(const char *s) { (void)s; }
void hal_delay_us(uint32_t us) { (void)us; }
