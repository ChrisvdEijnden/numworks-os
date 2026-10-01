/* ================================================================
 * NumWorks OS — battery, charger and USB power
 * File: hal/battery.c
 *
 * N0110 wiring (include/config.h):
 *  - PB1: the battery voltage through a 1:2 divider, on ADC1 channel 9,
 *    measured against a 2.8 V reference;
 *  - PE3: the charger's (RT9526A) CHG output, open drain: pulled low
 *    while charging, released when done or with no input power;
 *  - PA9: VBUS. On the first PCB version (0, or a blank OTP word)
 *    NumWorks sets it to alternate function 10 to sense VBUS; later
 *    boards use a plain input. The pin level reads the same either way.
 * The thresholds (3.62/3.7/3.8 V, 20 mV hysteresis) are the ones
 * NumWorks' firmware uses for its battery icon.
 * ================================================================ */
#include "battery.h"
#include "led.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

/* ADC1 (RM0431 §15.13) */
typedef struct {
    vu32 SR; vu32 CR1; vu32 CR2; vu32 SMPR1; vu32 SMPR2;
    vu32 JOFR[4]; vu32 HTR; vu32 LTR; vu32 SQR1; vu32 SQR2; vu32 SQR3;
    vu32 JSQR; vu32 JDR[4]; vu32 DR;
} ADC_TypeDef;
_Static_assert(offsetof(ADC_TypeDef, DR) == 0x4C, "ADC DR offset");
#define ADC1       ((ADC_TypeDef *)(APB2_BASE + 0x2000UL))
#define ADC_CCR    (*(volatile uint32_t *)(APB2_BASE + 0x2304UL))
#define ADC_SR_EOC (1U << 1)
#define ADC_CR2_ADON    (1U << 0)
#define ADC_CR2_SWSTART (1U << 30)
#define RCC_APB2ENR_ADC1EN (1U << 8)

/* OTP block 0: NumWorks' PCB version, stored inverted */
#define PCB_VERSION_OTP (*(volatile const uint32_t *)0x1FF07800UL)

/* Empty: below 3.6 V once the hysteresis is applied */
static const uint16_t THRESHOLDS_MV[3] = { 3620, 3700, 3800 };
#define HYSTERESIS_MV 20U

static battery_level_t s_level = BAT_FULL;
static uint32_t s_mv;
static bool s_charging, s_usb;
static bool s_ok;

static uint32_t pcb_version(void) {
    uint32_t v = ~PCB_VERSION_OTP;
    return v == 0xFFFFFFFFU ? 0U : v;
}

void battery_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOEEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    gpio_input(BAT_CHARGING_PORT, BAT_CHARGING_PIN, GPIO_PULL_UP);
    gpio_analog(BAT_SENSE_PORT, BAT_SENSE_PIN);
    if (pcb_version() == 0) gpio_af(USB_VBUS_PORT, USB_VBUS_PIN, 10U, 2U);
    else                    gpio_input(USB_VBUS_PORT, USB_VBUS_PIN, GPIO_PULL_NONE);

    /* ADC clock = APB2 / 4 = 24 MHz (36 MHz at most); one channel,
     * longest sampling time (480 cycles) for the divider's impedance */
    ADC_CCR = (ADC_CCR & ~(3U << 16)) | (1U << 16);
    ADC1->CR1  = 0;                              /* 12 bits */
    ADC1->SQR1 = 0;                              /* one conversion */
    ADC1->SQR3 = BAT_ADC_CHANNEL;
    ADC1->SMPR2 = (ADC1->SMPR2 & ~(7U << (BAT_ADC_CHANNEL * 3))) |
                  (7U << (BAT_ADC_CHANNEL * 3));
    ADC1->CR2  = ADC_CR2_ADON;
    for (volatile int i = 0; i < 2000; i++) {}   /* tSTAB: a few us */
    s_ok = true;

    s_mv = battery_mv();
    /* Start from the plain thresholds, without hysteresis */
    s_level = BAT_FULL;
    for (int i = 0; i < 3; i++)
        if (s_mv < THRESHOLDS_MV[i]) { s_level = (battery_level_t)i; break; }
    battery_poll();
}

static int convert(void) {
    ADC1->SR = 0;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    for (uint32_t n = 0; n < 100000U; n++)       /* ~21 us normally */
        if (ADC1->SR & ADC_SR_EOC) return (int)(ADC1->DR & 0xFFFU);
    return -1;
}

uint32_t battery_mv(void) {
    if (!s_ok) return 0;
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        int v = convert();
        if (v < 0) return 0;
        sum += (uint32_t)v;
    }
    /* VBAT = 2 x Vref x code / 4095 */
    return (2U * BAT_ADC_VREF_MV * sum / 8U + 2047U) / 4095U;
}

bool battery_charging(void)    { return !gpio_read(BAT_CHARGING_PORT, BAT_CHARGING_PIN); }
bool battery_usb_powered(void) { return gpio_read(USB_VBUS_PORT, USB_VBUS_PIN); }
battery_level_t battery_level(void) { return s_level; }

battery_level_t battery_level_for(uint32_t mv, battery_level_t previous) {
    /* Moving down past a threshold needs HYSTERESIS_MV below it, moving
     * up needs HYSTERESIS_MV above it */
    for (int i = 0; i < 3; i++) {
        uint32_t t = THRESHOLDS_MV[i];
        t = (i < (int)previous) ? t - HYSTERESIS_MV : t + HYSTERESIS_MV;
        if (mv < t) return (battery_level_t)i;
    }
    return BAT_FULL;
}

bool battery_poll(void) {
    if (!s_ok) return false;
    battery_level_t old_level = s_level;
    bool old_charging = s_charging, old_usb = s_usb;
    s_mv       = battery_mv();
    s_usb      = battery_usb_powered();
    s_charging = s_usb && battery_charging();
    s_level    = battery_level_for(s_mv, s_level);
    led_set_charge(!s_usb ? LED_CHARGE_NONE :
                   s_charging ? LED_CHARGE_CHARGING : LED_CHARGE_FULL);
    return s_level != old_level || s_charging != old_charging || s_usb != old_usb;
}
