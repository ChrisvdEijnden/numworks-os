/* ================================================================
 * NumWorks OS — system clocks
 * File: hal/clocks.c
 *
 * clocks_high(): 192 MHz from the PLL (at boot, and after sleep).
 * clocks_low():  16 MHz from HSI with the PLL, the crystal and the
 *                regulator's over-drive off, for sleep. USB needs the
 *                PLL's 48 MHz, so it doesn't work in this state.
 * Code runs from the QSPI flash throughout: its clock is HCLK/2, so it
 * just gets slower. Wait for the UART to finish sending before
 * switching, or the characters in flight come out garbled.
 * ================================================================ */
#include "clocks.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

/* Whether the PLL runs from the crystal (false: HSI fallback) */
bool g_boot_hse = false;
extern volatile uint32_t g_hclk_hz;

/* ================================================================
 * PLL setup: HSE 8 MHz → VCO 384 MHz → SYSCLK 192 MHz
 * APB1 = /4 = 48 MHz, APB2 = /2 = 96 MHz, PLLQ = /8 = 48 MHz for USB
 *
 * 192 MHz rather than the F730's maximum of 216 MHz, as in NumWorks'
 * own firmware: the QSPI flash we run from is clocked at HCLK/2 (set
 * up by the bootloader), and at 216 MHz that is 108 MHz, above the
 * AT25SF641's 104 MHz maximum. 192 MHz gives 96 MHz there, and an
 * exact 48 MHz for USB.
 *
 * We are normally started by a bootloader that may already be running
 * from its own PLL. The PLL can only be reconfigured while it is off,
 * so drop back to HSI first. 192 MHz is above the 180 MHz limit of
 * normal mode, so over-drive is enabled using the sequence from the
 * reference manual (RM0431, "Entering Over-drive mode").
 * ================================================================ */
#define HSE_STARTUP_SPINS 500000U

void clocks_high(void) {
    /* Flash: 6 wait states for 180-210 MHz at 2.7-3.6 V, enable ART +
     * prefetch. More wait states than needed is always safe, so set
     * them before raising the clock. */
    FLASH_R->ACR = FLASH_ACR_LATENCY(6) | FLASH_ACR_PRFTEN | FLASH_ACR_ARTEN;
    while ((FLASH_R->ACR & 0xFU) != 6U) {}

    /* Run from HSI and stop the PLL */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    RCC->CFGR &= ~RCC_CFGR_SW_MASK;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_HSI) {}
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {}

    /* Voltage scale 1 (VOS may only change while the PLL is off) */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS_MASK) | PWR_CR1_VOS_SCALE1;

    /* HSE, with a timeout: fall back to HSI rather than hang forever.
     * Either way the PLL input is 1 MHz. */
    uint32_t pllsrc = RCC_PLLCFGR_SRC_HSE;
    uint32_t pllm   = HSE_HZ / 1000000U;
    uint32_t spins  = 0;
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
        if (++spins > HSE_STARTUP_SPINS) {
            RCC->CR &= ~RCC_CR_HSEON;
            pllsrc = 0;      /* HSI */
            pllm   = 16U;
            break;
        }
    }
    g_boot_hse = (pllsrc != 0);

    /* PLLM=1 MHz in, PLLN=384, PLLP=/2 → 192 MHz, PLLQ=/8 → 48 MHz */
    RCC->PLLCFGR = (RCC->PLLCFGR & ~RCC_PLLCFGR_MASK)
                 | (pllm  << 0)    /* PLLM  */
                 | (384U  << 6)    /* PLLN  */
                 | (0U    << 16)   /* PLLP = /2 */
                 | pllsrc          /* PLL source */
                 | (8U    << 24);  /* PLLQ  */
    RCC->CR |= RCC_CR_PLLON;

    /* Over-drive: enable, then switch the regulator to it */
    PWR->CR1 |= PWR_CR1_ODEN;
    while (!(PWR->CSR1 & PWR_CSR1_ODRDY)) {}
    PWR->CR1 |= PWR_CR1_ODSWEN;
    while (!(PWR->CSR1 & PWR_CSR1_ODSWRDY)) {}

    /* Bus dividers: AHB=/1, APB1=/4, APB2=/2 */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_PRE_MASK)
              | (0U << 4)    /* HPRE  /1   */
              | (5U << 10)   /* PPRE1 /4   */
              | (4U << 13);  /* PPRE2 /2   */

    /* Wait for lock, then switch to PLL */
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MASK) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) {}
    g_hclk_hz = SYSCLK_HZ;
}

void clocks_low(void) {
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    RCC->CFGR &= ~RCC_CFGR_SW_MASK;                    /* HSI */
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_HSI) {}
    g_hclk_hz = 16000000UL;
    /* Leave over-drive (RM0431 §4.1.4): regulator first, then ODEN */
    PWR->CR1 &= ~PWR_CR1_ODSWEN;
    while (PWR->CSR1 & PWR_CSR1_ODSWRDY) {}
    PWR->CR1 &= ~PWR_CR1_ODEN;
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {}
    RCC->CR &= ~RCC_CR_HSEON;
}
