/* ================================================================
 * NumWorks OS — RGB status LED
 * File: hal/led.c
 *
 * Red, green and blue are driven by TIM3 channels 1-3 in PWM mode on
 * PB4, PB5 and PB0 (alternate function 2), as in NumWorks' N0110
 * board configuration; a channel lights while its output is high.
 * PWM lets each colour be dimmed: the LED is very bright at full duty.
 * ================================================================ */
#include "led.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

/* TIM3 counts at 2 x APB1 = 96 MHz; 20000 steps = 4.8 kHz, no flicker */
#define PWM_STEPS 20000U
#define CCMR_PWM1 6U              /* OCxM = 110: high while CNT < CCRx */
#define CCMR_PE   (1U << 3)       /* OCxPE: preload CCRx */

typedef struct { uint8_t r, g, b; } rgb_t;
static const rgb_t COLOURS[LED_COLOUR_COUNT] = {
    {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
};
static const rgb_t CHARGING = {255, 60, 0};     /* orange */
static const rgb_t FULL     = {0, 255, 0};

static led_colour_t s_colour = LED_OFF;
static led_charge_t s_charge = LED_CHARGE_NONE;
static bool s_suspended;
static bool s_ok;

/* Duty for a 0-255 value; at most a quarter of the period, which is
 * plenty for an indicator */
static uint32_t duty(uint8_t v) { return (uint32_t)v * (PWM_STEPS / 4U) / 255U; }

static void show(void) {
    if (!s_ok) return;
    rgb_t c = COLOURS[s_colour];
    if (s_charge == LED_CHARGE_CHARGING) c = CHARGING;
    else if (s_charge == LED_CHARGE_FULL) c = FULL;
    else if (s_suspended) c = COLOURS[LED_OFF];
    TIM3->CCR1 = duty(c.r);
    TIM3->CCR2 = duty(c.g);
    TIM3->CCR3 = duty(c.b);
}

void led_init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->APB1ENR;

    TIM3->CR1   = 0;
    TIM3->PSC   = 0;
    TIM3->ARR   = PWM_STEPS - 1U;
    TIM3->CCR1  = TIM3->CCR2 = TIM3->CCR3 = 0;
    TIM3->CCMR1 = (CCMR_PWM1 << 4) | CCMR_PE | (CCMR_PWM1 << 12) | (CCMR_PE << 8);  /* ch1, ch2 */
    TIM3->CCMR2 = (CCMR_PWM1 << 4) | CCMR_PE;                                       /* ch3 */
    TIM3->CCER  = (1U << 0) | (1U << 4) | (1U << 8);   /* CC1E CC2E CC3E, active high */
    TIM3->EGR   = TIM_EGR_UG;                          /* load PSC/ARR/CCRx */
    TIM3->CR1   = TIM_CR1_ARPE | TIM_CR1_CEN;

    gpio_af(LED_RED_PORT,   LED_RED_PIN,   2U, 0U);
    gpio_af(LED_GREEN_PORT, LED_GREEN_PIN, 2U, 0U);
    gpio_af(LED_BLUE_PORT,  LED_BLUE_PIN,  2U, 0U);
    s_ok = true;
    show();
}

bool led_available(void) { return s_ok; }

void led_set(led_colour_t colour) {
    if (colour >= LED_COLOUR_COUNT) return;
    s_colour = colour;
    show();
}

led_colour_t led_get(void) { return s_colour; }

void led_set_charge(led_charge_t state) {
    if (state == s_charge) return;
    s_charge = state;
    show();
}

void led_suspend(void) { s_suspended = true;  show(); }
void led_resume(void)  { s_suspended = false; show(); }
