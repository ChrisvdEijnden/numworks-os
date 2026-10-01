/* Minimal STM32F730 register definitions for NumWorks OS */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef volatile uint32_t vu32;
typedef volatile uint16_t vu16;
typedef volatile uint8_t  vu8;

#define PERIPH_BASE  0x40000000UL
#define APB1_BASE    (PERIPH_BASE + 0x00000000UL)
#define APB2_BASE    (PERIPH_BASE + 0x00010000UL)
#define AHB1_BASE    (PERIPH_BASE + 0x00020000UL)

/* RCC */
#define RCC_BASE (AHB1_BASE + 0x3800UL)
typedef struct {
    vu32 CR; vu32 PLLCFGR; vu32 CFGR; vu32 CIR;
    vu32 AHB1RSTR; vu32 AHB2RSTR; vu32 AHB3RSTR; uint32_t R0;
    vu32 APB1RSTR; vu32 APB2RSTR; uint32_t R1[2];
    vu32 AHB1ENR;  vu32 AHB2ENR;  vu32 AHB3ENR;  uint32_t R2;
    vu32 APB1ENR;  vu32 APB2ENR;
} RCC_TypeDef;
#define RCC ((RCC_TypeDef *)RCC_BASE)

#define RCC_CR_HSION        (1U<<0)
#define RCC_CR_HSIRDY       (1U<<1)
#define RCC_CR_HSEON        (1U<<16)
#define RCC_CR_HSERDY       (1U<<17)
#define RCC_CR_PLLON        (1U<<24)
#define RCC_CR_PLLRDY       (1U<<25)
#define RCC_CFGR_SW_MASK    (3U<<0)
#define RCC_CFGR_SW_PLL     (2U<<0)
#define RCC_CFGR_SWS_MASK   (3U<<2)
#define RCC_CFGR_SWS_HSI    (0U<<2)
#define RCC_CFGR_SWS_PLL    (2U<<2)
#define RCC_CFGR_PRE_MASK   ((0xFU<<4) | (7U<<10) | (7U<<13))  /* HPRE, PPRE1, PPRE2 */
#define RCC_PLLCFGR_SRC_HSE (1U<<22)
#define RCC_PLLCFGR_MASK    0x0F437FFFU   /* PLLM, PLLN, PLLP, PLLSRC, PLLQ */
#define RCC_AHB1ENR_GPIOAEN (1U<<0)
#define RCC_AHB1ENR_GPIOBEN (1U<<1)
#define RCC_AHB1ENR_GPIOCEN (1U<<2)
#define RCC_AHB1ENR_GPIODEN (1U<<3)
#define RCC_AHB1ENR_GPIOEEN (1U<<4)
#define RCC_APB1ENR_USART2EN (1U<<17)
#define RCC_APB1ENR_PWREN   (1U<<28)
#define RCC_APB2ENR_USART1EN (1U<<4)

/* PWR */
typedef struct { vu32 CR1; vu32 CSR1; vu32 CR2; vu32 CSR2; } PWR_TypeDef;
#define PWR ((PWR_TypeDef *)(APB1_BASE + 0x7000UL))
#define PWR_CR1_VOS_MASK    (3U<<14)
#define PWR_CR1_VOS_SCALE1  (3U<<14)
#define PWR_CR1_ODEN        (1U<<16)
#define PWR_CR1_ODSWEN      (1U<<17)
#define PWR_CSR1_ODRDY      (1U<<16)
#define PWR_CSR1_ODSWRDY    (1U<<17)

/* GPIO */
typedef struct {
    vu32 MODER; vu32 OTYPER; vu32 OSPEEDR; vu32 PUPDR;
    vu32 IDR;   vu32 ODR;    vu32 BSRR;    vu32 LCKR;
    vu32 AFR[2];
} GPIO_TypeDef;
#define GPIOA ((GPIO_TypeDef *)(AHB1_BASE + 0x0000UL))
#define GPIOB ((GPIO_TypeDef *)(AHB1_BASE + 0x0400UL))
#define GPIOC ((GPIO_TypeDef *)(AHB1_BASE + 0x0800UL))
#define GPIOD ((GPIO_TypeDef *)(AHB1_BASE + 0x0C00UL))
#define GPIOE ((GPIO_TypeDef *)(AHB1_BASE + 0x1000UL))

/* True if the pin is in alternate-function mode, i.e. already owned by a
 * peripheral (QSPI, FMC, USB...) that the bootloader or a driver set up.
 * Reconfiguring such a pin can kill the bus we are running from. */
static inline bool gpio_pin_is_af(const GPIO_TypeDef *p, uint32_t pin) {
    return ((p->MODER >> (pin * 2U)) & 3U) == 2U;
}

/* USART — STM32F7 layout (differs from F1/F4: no SR/DR, see RM0431) */
typedef struct {
    vu32 CR1;  vu32 CR2;  vu32 CR3; vu32 BRR;
    vu32 GTPR; vu32 RTOR; vu32 RQR; vu32 ISR;
    vu32 ICR;  vu32 RDR;  vu32 TDR;
} USART_TypeDef;
_Static_assert(offsetof(USART_TypeDef, BRR) == 0x0C, "USART BRR offset");
_Static_assert(offsetof(USART_TypeDef, ISR) == 0x1C, "USART ISR offset");
_Static_assert(offsetof(USART_TypeDef, TDR) == 0x28, "USART TDR offset");
#define USART1 ((USART_TypeDef *)(APB2_BASE + 0x1000UL))
#define USART2 ((USART_TypeDef *)(APB1_BASE + 0x4400UL))
#define USART_ISR_ORE    (1U<<3)
#define USART_ISR_RXNE   (1U<<5)
#define USART_ISR_TXE    (1U<<7)
#define USART_ICR_ORECF  (1U<<3)
#define USART_CR1_UE     (1U<<0)
#define USART_CR1_RE     (1U<<2)
#define USART_CR1_TE     (1U<<3)
#define USART_CR1_RXNEIE (1U<<5)

/* TIM */
typedef struct {
    vu32 CR1; vu32 CR2; vu32 SMCR; vu32 DIER;
    vu32 SR;  vu32 EGR; vu32 CCMR1; vu32 CCMR2;
    vu32 CCER; vu32 CNT; vu32 PSC;  vu32 ARR;
} TIM_TypeDef;
#define TIM6 ((TIM_TypeDef *)(APB1_BASE + 0x1000UL))
#define TIM_CR1_CEN  (1U<<0)
#define TIM_DIER_UIE (1U<<0)
#define TIM_EGR_UG   (1U<<0)
#define TIM_SR_UIF   (1U<<0)

/* Flash */
typedef struct {
    vu32 ACR; vu32 KEYR; vu32 OPTKEYR; vu32 SR;
    vu32 CR;  vu32 OPTCR;
} FLASH_TypeDef;
#define FLASH_R ((FLASH_TypeDef *)(AHB1_BASE + 0x3C00UL))
#define FLASH_KEY1   0x45670123UL
#define FLASH_KEY2   0xCDEF89ABUL
#define FLASH_CR_PG  (1U<<0)
#define FLASH_CR_SER (1U<<1)
#define FLASH_CR_SNB(n) ((n)<<3)
#define FLASH_CR_PSIZE_32 (2U<<8)
#define FLASH_CR_STRT (1U<<16)
#define FLASH_CR_LOCK (1U<<31)
#define FLASH_SR_EOP    (1U<<0)
#define FLASH_SR_OPERR  (1U<<1)
#define FLASH_SR_WRPERR (1U<<4)
#define FLASH_SR_PGAERR (1U<<5)
#define FLASH_SR_PGPERR (1U<<6)
#define FLASH_SR_ERSERR (1U<<7)
#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_PGPERR | FLASH_SR_ERSERR)
#define FLASH_SR_BSY    (1U<<16)
/* Device electronic signature: internal flash size in KB (RM0431, F72x/F73x) */
#define FLASH_SIZE_KB   (*(const volatile uint16_t *)0x1FF07A22UL)
#define FLASH_BASE_ADDR 0x08000000UL
#define FLASH_ACR_LATENCY(n) (n)
#define FLASH_ACR_PRFTEN (1U<<8)
#define FLASH_ACR_ARTEN  (1U<<9)

/* SysTick */
#define SysTick_BASE 0xE000E010UL
typedef struct { vu32 CTRL; vu32 LOAD; vu32 VAL; vu32 CALIB; } SysTick_Type;
#define SysTick ((SysTick_Type *)SysTick_BASE)
#define SysTick_CTRL_CLKSOURCE (1U<<2)
#define SysTick_CTRL_TICKINT   (1U<<1)
#define SysTick_CTRL_ENABLE    (1U<<0)

/* System control block: caches */
#define SCB_CCR       (*(volatile uint32_t *)0xE000ED14UL)
#define SCB_CCR_IC    (1U<<17)
#define SCB_ICIALLU   (*(volatile uint32_t *)0xE000EF50UL)  /* invalidate I-cache */

/* MPU (ARMv7-M) */
#define MPU_TYPE      (*(volatile uint32_t *)0xE000ED90UL)
#define MPU_CTRL      (*(volatile uint32_t *)0xE000ED94UL)
#define MPU_RNR       (*(volatile uint32_t *)0xE000ED98UL)
#define MPU_RBAR      (*(volatile uint32_t *)0xE000ED9CUL)
#define MPU_RASR      (*(volatile uint32_t *)0xE000EDA0UL)
#define MPU_CTRL_ENABLE      (1U<<0)
#define MPU_CTRL_PRIVDEFENA  (1U<<2)   /* default map where no region matches */
#define MPU_RASR_ENABLE      (1U<<0)
#define MPU_RASR_SIZE(log2)  (((log2) - 1U) << 1)   /* region = 2^log2 bytes */
#define MPU_RASR_B           (1U<<16)
#define MPU_RASR_AP_FULL     (3U<<24)
#define MPU_RASR_XN          (1U<<28)

/* NVIC */
#define NVIC_BASE 0xE000E100UL
typedef struct { vu32 ISER[8]; uint32_t R[24]; vu32 ICER[8]; } NVIC_Type;
#define NVIC ((NVIC_Type *)NVIC_BASE)
static inline void nvic_enable(uint8_t n)  { NVIC->ISER[n>>5] = 1U<<(n&31); }
static inline void nvic_disable(uint8_t n) { NVIC->ICER[n>>5] = 1U<<(n&31); }
