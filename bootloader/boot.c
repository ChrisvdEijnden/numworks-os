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
#include <stdbool.h>
#include "../include/stm32f730.h"
#include "../include/config.h"

/* Forward declarations */
static void clocks_init(void);
static void mpu_init(void);
static void icache_enable(void);
static void systick_init(void);
static void gpio_init(void);

/* Tick counter — updated by SysTick_Handler in kernel/kernel.c */
volatile uint32_t g_tick_ms = 0;
volatile uint32_t g_tick_step = 1;   /* ms per SysTick (more while asleep) */

/* Whether the PLL runs from the crystal (false: HSI fallback) */
bool g_boot_hse = false;

/* ================================================================
 * boot_main — entry from startup.s
 * ================================================================ */
void boot_main(void) {
    extern void hal_stack_paint(void);
    hal_stack_paint();      /* first, so `mem` can report the stack peak */
    /* Report MemManage, BusFault and UsageFault as themselves rather
     * than as a HardFault (SCB->SHCSR) */
    *(volatile uint32_t *)0xE000ED24UL |= (1U << 16) | (1U << 17) | (1U << 18);
    clocks_init();
    mpu_init();
    icache_enable();
    systick_init();
    gpio_init();
    extern int main(void);
    main();   /* Should never return */
    while (1) {}
}

/* ================================================================
 * PLL setup: HSE 8 MHz → VCO 432 MHz → SYSCLK 216 MHz
 * APB1 = /4 = 54 MHz, APB2 = /2 = 108 MHz, PLLQ = 48 MHz for USB
 *
 * We are normally started by a bootloader that may already be running
 * from its own PLL. The PLL can only be reconfigured while it is off,
 * so drop back to HSI first. 216 MHz is above the 180 MHz limit of
 * normal mode, so over-drive is enabled using the sequence from the
 * reference manual (RM0431, "Entering Over-drive mode").
 * ================================================================ */
#define HSE_STARTUP_SPINS 500000U

static void clocks_init(void) {
    /* Flash: 7 wait states for 216 MHz, enable ART + prefetch.
     * More wait states than needed is always safe, so set them first. */
    FLASH_R->ACR = FLASH_ACR_LATENCY(7) | FLASH_ACR_PRFTEN | FLASH_ACR_ARTEN;
    while ((FLASH_R->ACR & 0xFU) != 7U) {}

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

    /* PLLM=1 MHz in, PLLN=432, PLLP=/2 → 216 MHz, PLLQ=/9 → 48 MHz */
    RCC->PLLCFGR = (RCC->PLLCFGR & ~RCC_PLLCFGR_MASK)
                 | (pllm  << 0)    /* PLLM  */
                 | (432U  << 6)    /* PLLN  */
                 | (0U    << 16)   /* PLLP = /2 */
                 | pllsrc          /* PLL source */
                 | (9U    << 24);  /* PLLQ  */
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
}

/* ================================================================
 * MPU: the default Cortex-M7 memory map treats 0x6000_0000 (FMC bank 1,
 * the LCD) as Normal memory, where the core may merge, reorder or
 * speculatively issue accesses — wrong for an LCD command/data register
 * pair. Map it as Device memory, execute-never. Regions a bootloader
 * left behind are cleared first so the result is the default map plus
 * this one region, whatever ran before us.
 * ================================================================ */
static void mpu_init(void) {
    __asm volatile("dmb" ::: "memory");
    MPU_CTRL = 0;
    uint32_t nregions = (MPU_TYPE >> 8) & 0xFFU;
    for (uint32_t i = 0; i < nregions; i++) {
        MPU_RNR  = i;
        MPU_RASR = 0;
    }
    if (nregions > 0) {
        MPU_RNR  = 0;
        MPU_RBAR = 0x60000000UL;                 /* FMC bank 1, 256 MB */
        MPU_RASR = MPU_RASR_XN | MPU_RASR_AP_FULL | MPU_RASR_B |  /* Device */
                   MPU_RASR_SIZE(28) | MPU_RASR_ENABLE;
        MPU_CTRL = MPU_CTRL_PRIVDEFENA | MPU_CTRL_ENABLE;
    }
    __asm volatile("dsb\n isb" ::: "memory");
}

/* The I-cache matters most here: code runs from QSPI flash. The D-cache
 * stays off; enabling it needs cache maintenance around flash writes
 * (fs/flashfs.c) and any future DMA. */
static void icache_enable(void) {
    __asm volatile("dsb\n isb" ::: "memory");
    SCB_ICIALLU = 0;
    __asm volatile("dsb\n isb" ::: "memory");
    SCB_CCR |= SCB_CCR_IC;
    __asm volatile("dsb\n isb" ::: "memory");
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
