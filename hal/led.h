#pragma once
#include <stdbool.h>

/* RGB status LED (LTST-S310F2KT) */
typedef enum { LED_OFF, LED_RED, LED_GREEN, LED_BLUE, LED_WHITE, LED_COLOUR_COUNT } led_colour_t;

/* What the charger is doing; while USB power is present this is shown
 * instead of the chosen colour (orange: charging, green: full) */
typedef enum { LED_CHARGE_NONE, LED_CHARGE_CHARGING, LED_CHARGE_FULL } led_charge_t;

void led_init(void);
bool led_available(void);
void led_set(led_colour_t colour);       /* the user's colour */
led_colour_t led_get(void);
void led_set_charge(led_charge_t state);
/* While the calculator sleeps only the charge state is shown */
void led_suspend(void);
void led_resume(void);
