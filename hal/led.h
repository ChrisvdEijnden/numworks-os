#pragma once
#include <stdbool.h>

/* RGB status LED (LTST-S310F2KT) */
typedef enum { LED_OFF, LED_RED, LED_GREEN, LED_BLUE, LED_WHITE, LED_COLOUR_COUNT } led_colour_t;

void led_init(void);
bool led_available(void);        /* false until the pins are set in config.h */
void led_set(led_colour_t colour);
