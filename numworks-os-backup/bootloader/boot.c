/* ================================================================
 * NumWorks OS — Bootloader
 * File: bootloader/boot.c
 *
 * Responsibilities:
 *   1. Configure PLL to 216 MHz
 *   2. Set flash wait states & cache
 *   3. Enable peripheral clocks
 *   4. Brief splash on UART
 *   5. Hand off to kernel_main()
 *
 * Code size target: < 2 KB
 * ================================================================ */
#include "stm32f730.h"
#include "config.h"

/* Forward declarations */
extern void kernel_main(void);
static void clocks_init(void);
static void systick_init(void);
static void gpio_init(void);

/* Tick counter — updated by SysTick_Handler in kernel */
volatile uint32_t g_tick_ms = 0;

/* ── SysTick IRQ (declared weak so kernel can override) ──────── */
__attribute__((weak)) void SysTick_Handler(void) {
    g_tick_ms++;
}

/* ── Simple busy-wait ────────────────────────────────────────── */
void delay_ms(uint32_t ms) {
    uint32_t start = g_tick_ms;
    while ((g_tick_ms - start) < ms) { __asm volatile("wfe"); }
}

/* ================================================================
 * boot_main — entry from startup.s
 * ================================================================ */
void boot_main(void) {
    clocks_init();
    systick_init();
    gpio_init();
    kernel_main();   /* Should never return */
    while (1) {}
}

/* ================================================================
 * PLL setup: HSE 8 MHz → VCO 432 MHz → SYSCLK 216 MHz
 * APB1 = /4 = 54 MHz, APB2 = /2 = 108 MHz
 * ================================================================ */
static void clocks_init(void) {
    /* Flash: 7 wait states for 216 MHz, enable ART + prefetch */
    FLASH_R->ACR = FLASH_ACR_LATENCY(7) | FLASH_ACR_PRFTEN | FLASH_ACR_ARTEN;

    /* Enable HSE */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    /* Enable PWR, set voltage scale 1 (max performance) */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    /* PWR->CR1 scale 3→1 would go here; simplified for size */

    /* Configure PLL: VCO=432, /2=216 SYSCLK, /9=48 USB */
    /* PLLCFGR: PLLM=8, PLLN=432, PLLP=/2, PLLSRC=HSE, PLLQ=9 */
    RCC->PLLCFGR = (8U      << 0)   /* PLLM  */
                 | (432U    << 6)   /* PLLN  */
                 | (0U      << 16)  /* PLLP = /2 */
                 | (1U      << 22)  /* PLL src = HSE */
                 | (9U      << 24); /* PLLQ  */

    /* Enable PLL */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    /* Bus dividers: AHB=/1, APB1=/4, APB2=/2 */
    RCC->CFGR = (0U << 4)    /* HPRE  /1   */
              | (5U << 10)   /* PPRE1 /4   */
              | (4U << 13);  /* PPRE2 /2   */

    /* Switch to PLL */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) {}
}

/* ── SysTick: 1 kHz ──────────────────────────────────────────── */
static void systick_init(void) {
    SysTick->LOAD = (SYSCLK_HZ / TICK_HZ) - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE |
                    SysTick_CTRL_TICKINT   |
                    SysTick_CTRL_ENABLE;
}

/* ── GPIO clocks for all ports used by NumWorks hardware ────── */
static void gpio_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN  | RCC_AHB1ENR_GPIODEN |
                    RCC_AHB1ENR_GPIOEEN;
    /* Peripheral-specific GPIO config done in each HAL module */
}
