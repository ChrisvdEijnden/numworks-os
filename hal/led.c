/* ================================================================
 * NumWorks OS — RGB status LED
 * File: hal/led.c
 *
 * LTST-S310F2KT: common anode (pin 4), cathodes R/G/B on pins 1/2/3.
 * A colour lights when its cathode is pulled LOW, so the pins are
 * driven low for "on" and high for "off". White is all three. (Green
 * and blue need up to 3.8 V forward voltage, so they may be dim if the
 * anode is at 3.3 V.)
 *
 * Disabled until LED_CONFIGURED is set in include/config.h with the
 * board's real pins: driving a wrong pin can fight another chip.
 * ================================================================ */
#include "led.h"
#include "uart.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

#if LED_CONFIGURED

#if !defined(LED_R_PORT_NUM) || !defined(LED_R_PIN) || !defined(LED_G_PORT_NUM) || \
    !defined(LED_G_PIN) || !defined(LED_B_PORT_NUM) || !defined(LED_B_PIN)
#error "LED_CONFIGURED needs LED_{R,G,B}_PORT_NUM and LED_{R,G,B}_PIN in config.h"
#endif

static const struct { uint8_t port, pin; } PINS[3] = {
    { LED_R_PORT_NUM, LED_R_PIN }, { LED_G_PORT_NUM, LED_G_PIN }, { LED_B_PORT_NUM, LED_B_PIN },
};
static bool s_ok = false;
static led_colour_t s_colour = LED_OFF;
static bool s_suspended = false;

static GPIO_TypeDef *port(uint8_t n) {
    return (GPIO_TypeDef *)(AHB1_BASE + 0x400UL * n);
}

void led_init(void) {
    s_ok = true;
    for (int i = 0; i < 3; i++) {
        RCC->AHB1ENR |= 1U << PINS[i].port;
        GPIO_TypeDef *p = port(PINS[i].port);
        uint32_t pin = PINS[i].pin;
        if (gpio_pin_is_af(p, pin)) {          /* owned by another peripheral */
            hal_uart_puts("led: pin owned by another peripheral, LED disabled\n");
            s_ok = false;
            return;
        }
    }
    for (int i = 0; i < 3; i++) {
        GPIO_TypeDef *p = port(PINS[i].port);
        uint32_t pin = PINS[i].pin;
        p->BSRR  = 1U << pin;                   /* high = off */
        p->MODER = (p->MODER & ~(3U << (pin * 2))) | (1U << (pin * 2));
    }
}

bool led_available(void) { return s_ok; }

static void drive(led_colour_t colour) {
    static const uint8_t MASK[LED_COLOUR_COUNT] = { 0, 1, 2, 4, 7 };   /* bit 0=R 1=G 2=B */
    for (int i = 0; i < 3; i++) {
        uint32_t pin = PINS[i].pin;
        bool on = MASK[colour] & (1U << i);
        port(PINS[i].port)->BSRR = on ? (1U << (pin + 16)) : (1U << pin);   /* low = on */
    }
}

void led_set(led_colour_t colour) {
    if (!s_ok || colour >= LED_COLOUR_COUNT) return;
    s_colour = colour;
    if (!s_suspended) drive(colour);
}

led_colour_t led_get(void) { return s_colour; }
void led_suspend(void) { s_suspended = true;  if (s_ok) drive(LED_OFF); }
void led_resume(void)  { s_suspended = false; if (s_ok) drive(s_colour); }

#else

void led_init(void) {}
bool led_available(void) { return false; }
void led_set(led_colour_t colour) { (void)colour; }
led_colour_t led_get(void) { return LED_OFF; }
void led_suspend(void) {}
void led_resume(void) {}

#endif
