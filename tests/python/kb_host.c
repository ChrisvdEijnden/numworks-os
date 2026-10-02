/* key_to_char from the real driver; time and UART come from the test */
#include "../../include/stm32f730.h"
uint32_t hal_tick_ms(void); void hal_delay_ms(uint32_t); void hal_uart_puts(const char *);
#include "../../hal/keyboard.c"
void hal_uart_puts(const char *s) { (void)s; }
