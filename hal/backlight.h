#pragma once
#include <stdbool.h>
#include <stdint.h>

/* LCD backlight (enable pin PE0). 16 brightness levels, 0 (dimmest)
 * to BACKLIGHT_MAX. */
#define BACKLIGHT_MAX 15

void    backlight_init(void);           /* on, at the default level */
void    backlight_set_level(uint8_t level);
uint8_t backlight_level(void);
void    backlight_power(bool on);       /* off while asleep; on restores the level */
