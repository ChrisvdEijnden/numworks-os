/* ================================================================
 * NumWorks OS — LCD backlight
 * File: hal/backlight.c
 *
 * The backlight driver is controlled through one pin (PE0, from the
 * N0110 board configuration in NumWorks' Epsilon 15): high switches
 * it on at its brightest level, and each short low pulse (20 us low,
 * 20 us high) steps one level down; below the dimmest level it wraps
 * round to the brightest. Holding the pin low for a few milliseconds
 * switches it off. The driver can't be read back, so we count.
 * ================================================================ */
#include "backlight.h"
#include "hal.h"
#include "../include/stm32f730.h"
#include "../include/config.h"

#define LEVELS        (BACKLIGHT_MAX + 1)
#define DEFAULT_LEVEL 12

static uint8_t s_level  = BACKLIGHT_MAX;   /* the driver's level (when on) */
static uint8_t s_wanted = DEFAULT_LEVEL;
static bool    s_on;

static void pulse(void) {
    /* An interrupt stretching the low phase towards the off time would
     * switch the backlight off, so keep it short */
    uint32_t primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    gpio_write(BACKLIGHT_PORT, BACKLIGHT_PIN, false);
    hal_delay_us(20);
    gpio_write(BACKLIGHT_PORT, BACKLIGHT_PIN, true);
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
    hal_delay_us(20);
}

static void apply(void) {
    unsigned n = s_level >= s_wanted ? s_level - s_wanted
                                     : LEVELS + s_level - s_wanted;
    for (unsigned i = 0; i < n; i++) pulse();
    s_level = s_wanted;
}

void backlight_power(bool on) {
    if (on == s_on) return;
    s_on = on;
    if (on) {
        gpio_write(BACKLIGHT_PORT, BACKLIGHT_PIN, true);
        hal_delay_us(50);
        s_level = BACKLIGHT_MAX;               /* the driver starts at full */
        apply();
    } else {
        gpio_write(BACKLIGHT_PORT, BACKLIGHT_PIN, false);
        hal_delay_us(3000);
    }
}

void backlight_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
    (void)RCC->AHB1ENR;
    /* The bootloader may have left it on, at a level we can't know:
     * switch it off first so we start from a known state */
    gpio_output(BACKLIGHT_PORT, BACKLIGHT_PIN, false);
    hal_delay_us(3000);
    s_on = false;
    backlight_power(true);
}

void backlight_set_level(uint8_t level) {
    if (level > BACKLIGHT_MAX) level = BACKLIGHT_MAX;
    s_wanted = level;
    if (s_on) apply();
}

uint8_t backlight_level(void) { return s_wanted; }
