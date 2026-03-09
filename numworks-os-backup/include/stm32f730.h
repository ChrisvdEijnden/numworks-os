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
#define RCC_CFGR_SW_PLL     (2U<<0)
#define RCC_CFGR_SWS_MASK   (3U<<2)
#define RCC_CFGR_SWS_PLL    (2U<<2)
#define RCC_AHB1ENR_GPIOAEN (1U<<0)
#define RCC_AHB1ENR_GPIOBEN (1U<<1)
#define RCC_AHB1ENR_GPIOCEN (1U<<2)
#define RCC_AHB1ENR_GPIODEN (1U<<3)
#define RCC_AHB1ENR_GPIOEEN (1U<<4)
#define RCC_APB1ENR_USART2EN (1U<<17)
#define RCC_APB1ENR_PWREN   (1U<<28)
#define RCC_APB2ENR_USART1EN (1U<<4)

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

/* USART */
typedef struct {
    vu32 SR; vu32 DR; vu32 BRR; vu32 CR1; vu32 CR2; vu32 CR3; vu32 GTPR;
} USART_TypeDef;
#define USART1 ((USART_TypeDef *)(APB2_BASE + 0x1000UL))
#define USART2 ((USART_TypeDef *)(APB1_BASE + 0x4400UL))
#define USART_SR_TXE  (1U<<7)
#define USART_SR_RXNE (1U<<5)
#define USART_CR1_UE  (1U<<13)
#define USART_CR1_TE  (1U<<3)
#define USART_CR1_RE  (1U<<2)
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
#define FLASH_SR_BSY  (1U<<16)
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

/* NVIC */
#define NVIC_BASE 0xE000E100UL
typedef struct { vu32 ISER[8]; uint32_t R[24]; vu32 ICER[8]; } NVIC_Type;
#define NVIC ((NVIC_Type *)NVIC_BASE)
static inline void nvic_enable(uint8_t n)  { NVIC->ISER[n>>5] = 1U<<(n&31); }
static inline void nvic_disable(uint8_t n) { NVIC->ICER[n>>5] = 1U<<(n&31); }
