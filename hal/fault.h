#pragma once
#include <stdint.h>

/* Show a crash screen (and the same text on the debug UART), wait for a
 * key, then reset. Used by the CPU fault handlers and for fatal errors. */
__attribute__((noreturn)) void hal_panic(const char *msg);

/* Software reset */
__attribute__((noreturn)) void hal_reset(void);

/* Called by Fault_Handler in startup_stm32f730.s */
__attribute__((noreturn)) void fault_report(const uint32_t *frame, uint32_t ipsr);
