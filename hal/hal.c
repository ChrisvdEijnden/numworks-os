/* hal_init: initialises debug UART. Display, keyboard, and timer
 * are initialised separately by main() to allow splash ordering. */
#include "hal.h"
#include "uart.h"
#include "display.h"
#include "keyboard.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

extern volatile uint32_t g_tick_ms;
extern bool g_boot_hse;
extern volatile uint32_t g_hclk_hz;

/* RCC_CSR reset flags (RM0431 §5.3.21); RMVF clears them */
#define RCC_CSR (*(volatile uint32_t *)(RCC_BASE + 0x74UL))
#define RCC_CSR_RMVF (1U << 24)

static const char *reset_cause(uint32_t csr) {
    if (csr & (1U << 29)) return "watchdog (IWDG)";
    if (csr & (1U << 30)) return "watchdog (WWDG)";
    if (csr & (1U << 31)) return "low-power";
    if (csr & (1U << 28)) return "software (reboot or crash)";
    if (csr & (1U << 25)) return "power on / brown-out";
    if (csr & (1U << 26)) return "reset pin";
    return "unknown";
}

static bool s_wdg_reset;

bool hal_reset_by_watchdog(void) { return s_wdg_reset; }

void hal_watchdog_start(void) {
#if WATCHDOG_ENABLED
    DBGMCU_APB1_FZ |= DBGMCU_APB1_FZ_IWDG_STOP;   /* paused while a debugger halts us */
    IWDG->KR  = 0xCCCCU;                          /* start (also starts the LSI) */
    IWDG->KR  = 0x5555U;                          /* unlock PR/RLR */
    IWDG->PR  = 4U;                               /* /64: 500 Hz */
    IWDG->RLR = 4095U;                            /* ~8 s */
    for (uint32_t n = 0; n < 1000000U && IWDG->SR; n++) {}   /* registers updated */
    IWDG->KR  = 0xAAAAU;
#endif
}

/* Fed even with WATCHDOG_ENABLED 0: a watchdog started earlier (by a
 * bootloader) can't be stopped, and feeding a stopped one does nothing. */
void hal_watchdog_feed(void) {
    IWDG->KR = 0xAAAAU;
}

void hal_init(void) {
    /* UART for debug output */
    hal_uart_init();
    hal_uart_puts("\r\nNumWorks OS v" NWOS_VERSION " booting...\r\n");
    hal_uart_puts("reset cause: ");
    s_wdg_reset = (RCC_CSR & (1U << 29)) != 0;
    hal_uart_puts(reset_cause(RCC_CSR));
    hal_uart_puts(g_boot_hse ? "\nclock: 192 MHz from HSE\n"
                             : "\nclock: 192 MHz from HSI (crystal did not start)\n");
    RCC_CSR |= RCC_CSR_RMVF;
}

void hal_boot_log(const char *stage) {
    char ms[11];
    uint32_t v = g_tick_ms;
    int i = 10;
    ms[i] = 0;
    do { ms[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    hal_uart_puts("[boot ");
    for (int pad = 10 - i; pad < 5; pad++) hal_uart_putc(' ');
    hal_uart_puts(ms + i);
    hal_uart_puts(" ms] ");
    hal_uart_puts(stage);
    hal_uart_putc('\n');
}

/* ── Stack high-water mark ───────────────────────────────────────
 * The stack grows down from _estack to _sstack. At boot everything
 * below the current stack pointer is filled with a pattern; the lowest
 * word that no longer holds it is the deepest the stack has been. */
#define STACK_PAINT 0x5A7AC4EDUL
extern uint32_t _sstack[], _estack[];

void hal_stack_paint(void) {
    uint32_t *sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    for (uint32_t *p = _sstack; p < sp - 16; p++) *p = STACK_PAINT;
}

void hal_stack_stats(uint32_t *peak, uint32_t *size) {
    uint32_t *p = _sstack;
    while (p < _estack && *p == STACK_PAINT) p++;
    if (peak) *peak = (uint32_t)((_estack - p) * sizeof(uint32_t));
    if (size) *size = (uint32_t)((_estack - _sstack) * sizeof(uint32_t));
}

/* g_tick_ms keeps counting milliseconds: each tick adds the period */
void hal_tick_set_period(uint32_t ms) {
    extern volatile uint32_t g_tick_step;
    if (ms < 1) ms = 1;
    if (ms > 80) ms = 80;                       /* 24-bit reload at 192 MHz */
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE;
    SysTick->LOAD = (g_hclk_hz / 1000U) * ms - 1U;
    SysTick->VAL  = 0;
    g_tick_step   = ms;
    SysTick->CTRL |= SysTick_CTRL_ENABLE;
}

/* The millisecond count plus how far SysTick is into the current tick */
uint32_t hal_tick_us(void) {
    uint32_t ms, val;
    do {
        ms  = g_tick_ms;
        val = SysTick->VAL;
    } while (ms != g_tick_ms);                  /* a tick came in between */
    /* SysTick counts CPU cycles down from LOAD */
    return ms * 1000U + (SysTick->LOAD - val) / (g_hclk_hz / 1000000U);
}

/* Busy-wait on the cycle counter: needs neither SysTick nor
 * interrupts, so the crash screen can use it too */
void hal_delay_us(uint32_t us) {
    uint32_t start = DWT_CYCCNT;
    uint32_t n = us * (g_hclk_hz / 1000000U);
    while (DWT_CYCCNT - start < n) {}
}

uint32_t hal_tick_ms(void) {
    return g_tick_ms;
}

void hal_delay_ms(uint32_t ms) {
    hal_watchdog_feed();
    uint32_t start = g_tick_ms;
    while ((g_tick_ms - start) < ms) {
        __asm volatile("wfi");
    }
}
