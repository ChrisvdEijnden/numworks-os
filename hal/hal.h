#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "fault.h"

/* Subsystem init: debug UART, then the boot banner and reset cause */
void hal_init(void);

/* Independent watchdog: started once, can't be stopped. Anything that
 * blocks for long must feed it (hal_delay_ms does). */
void hal_watchdog_start(void);
void hal_watchdog_feed(void);
bool hal_reset_by_watchdog(void);   /* this boot followed a watchdog reset */

/* One line on the debug UART: "[boot   123 ms] <stage>" */
void hal_boot_log(const char *stage);

/* Memory: stack high-water mark (stack is painted at boot), C heap */
void     hal_stack_paint(void);
void     hal_stack_stats(uint32_t *peak, uint32_t *size);
void     hal_heap_stats(uint32_t *used, uint32_t *total);

/* Timing */
uint32_t hal_tick_ms(void);
uint32_t hal_tick_us(void);                  /* microseconds, wraps every ~71 min */
void     hal_delay_ms(uint32_t ms);
void     hal_delay_us(uint32_t us);            /* busy-waits; works with interrupts off */
void     hal_tick_set_period(uint32_t ms);   /* SysTick period, 1..80 ms */

/* Debug UART */
void hal_uart_init(void);
void hal_uart_putc(char c);
void hal_uart_puts(const char *s);
void hal_uart_flush(void);          /* wait until everything is sent */
int  hal_uart_getc(void);
int  hal_uart_available(void);
