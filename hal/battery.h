#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Battery voltage, charger state and USB power (hal/battery.c) */
typedef enum { BAT_EMPTY, BAT_LOW, BAT_MEDIUM, BAT_FULL } battery_level_t;

void     battery_init(void);
uint32_t battery_mv(void);              /* battery voltage, millivolts */
bool     battery_charging(void);        /* charger says it's charging */
bool     battery_usb_powered(void);     /* 5 V present on the USB port */
/* Level from the voltage, with hysteresis so it doesn't flicker
 * between two levels; updated by battery_poll() */
battery_level_t battery_level(void);
/* Call about once a second: measures, updates the level and the LED's
 * charge colour. True if anything shown on screen changed. */
bool     battery_poll(void);
/* Pure function behind battery_level(), for tests */
battery_level_t battery_level_for(uint32_t mv, battery_level_t previous);
