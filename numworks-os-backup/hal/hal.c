/* ================================================================
 * NumWorks OS — HAL Initialization
 * File: hal/hal.c
 * ================================================================ */
#include "hal.h"
#include "uart.h"
#include "timer.h"
#include "display.h"
#include "keyboard.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

extern volatile uint32_t g_tick_ms;

void hal_init(void) {
    /* UART for debug output */
    hal_uart_init();
    hal_uart_puts("\r\nNumWorks OS v0.1 booting...\r\n");
}

uint32_t hal_tick_ms(void) {
    return g_tick_ms;
}

void hal_delay_ms(uint32_t ms) {
    uint32_t start = g_tick_ms;
    while ((g_tick_ms - start) < ms) {
        __asm volatile("wfi");
    }
}

void hal_led_set(bool on) {
    /* NumWorks has no user LED, but PA15 can be used */
    if (on)
        GPIOA->BSRR = (1U << 15);
    else
        GPIOA->BSRR = (1U << (15 + 16));
}
