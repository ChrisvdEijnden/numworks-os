/* ================================================================
 * NumWorks OS — Bootloader
 * File: bootloader/boot.c
 *
 * Responsibilities:
 *   1. Configure PLL to 192 MHz
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
#include "../hal/clocks.h"

/* Forward declarations */
static void mpu_init(void);
static void icache_enable(void);
static void systick_init(void);
static void gpio_init(void);

/* Tick counter — updated by SysTick_Handler in kernel/kernel.c */
volatile uint32_t g_tick_ms = 0;
volatile uint32_t g_tick_step = 1;   /* ms per SysTick (more while asleep) */

/* Current core clock; lower while asleep (kernel/kernel.c) */
volatile uint32_t g_hclk_hz = SYSCLK_HZ;

/* ================================================================
 * boot_main — entry from startup.s
 * ================================================================ */
void boot_main(void) {
    extern void hal_stack_paint(void);
    hal_stack_paint();      /* first, so `mem` can report the stack peak */
    /* Report MemManage, BusFault and UsageFault as themselves rather
     * than as a HardFault (SCB->SHCSR) */
    *(volatile uint32_t *)0xE000ED24UL |= (1U << 16) | (1U << 17) | (1U << 18);
    clocks_high();
    /* Cycle counter, for hal_delay_us() */
    DEMCR    |= DEMCR_TRCENA;
    DWT_LAR   = 0xC5ACCE55U;          /* unlock (Cortex-M7) */
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    mpu_init();
    icache_enable();
    systick_init();
    gpio_init();
    extern int main(void);
    main();   /* Should never return */
    while (1) {}
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
