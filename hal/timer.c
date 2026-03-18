/* TIM6 as free-running microsecond counter */
#include "timer.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

static volatile uint32_t s_ovf = 0;

void hal_timer_init(void) {
    RCC->APB1ENR |= (1U << 4);  /* TIM6EN */
    TIM6->PSC  = (uint16_t)(APB1_HZ / 1000000U) - 1;  /* 1 MHz tick */
    TIM6->ARR  = 0xFFFF;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->CR1  = TIM_CR1_CEN;
    nvic_enable(54);  /* TIM6 IRQ */
}

void TIM6_DAC_IRQHandler(void) {
    TIM6->SR &= ~TIM_SR_UIF;
    s_ovf++;
}

uint32_t hal_micros(void) {
    uint32_t ovf = s_ovf;
    uint32_t cnt = TIM6->CNT;
    if (TIM6->SR & TIM_SR_UIF) { ovf++; cnt = TIM6->CNT; }
    return (ovf << 16) | cnt;
}
