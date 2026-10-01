#pragma once
#include <stdbool.h>

void clocks_high(void);   /* 192 MHz (PLL from HSE, or HSI if the crystal fails) */
void clocks_low(void);    /* 16 MHz HSI, PLL off: for sleep */
extern bool g_boot_hse;   /* the PLL runs from the crystal */
