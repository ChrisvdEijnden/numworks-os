/* TIM6 as free-running microsecond counter */
#include "timer.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

static volatile uint32_t s_ovf = 0;

void hal_timer_init(void) {
    RCC->APB1ENR |= (1U << 4);  /* TIM6EN */
    /* APB1 prescaler is /4, so timers on APB1 are clocked at 2 x APB1 */
    TIM6->PSC  = (uint16_t)(2U * APB1_HZ / 1000000U) - 1;  /* 1 MHz tick */
    TIM6->ARR  = 0xFFFF;
    TIM6->EGR  = TIM_EGR_UG;    /* load PSC now, not after the first overflow */
    TIM6->SR   = 0;             /* drop the UIF raised by UG */
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->CR1  = TIM_CR1_CEN;
    nvic_enable(54);  /* TIM6 IRQ */
}

void TIM6_DAC_IRQHandler(void) {
    TIM6->SR = ~TIM_SR_UIF;     /* rc_w0: write 0 to clear, 1 elsewhere */
    s_ovf++;
}

uint32_t hal_micros(void) {
    uint32_t ovf = s_ovf;
    uint32_t cnt = TIM6->CNT;
    if (TIM6->SR & TIM_SR_UIF) { ovf++; cnt = TIM6->CNT; }
    return (ovf << 16) | cnt;
}
