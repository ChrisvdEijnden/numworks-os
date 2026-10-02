/* ================================================================
 * NumWorks OS — simulator: the key matrix
 *
 * hal/keyboard.c is the real driver, scanning, debouncing and
 * repeating as on the calculator. It drives the rows on port A low one
 * at a time and reads the columns on port C; here a column reads low
 * when a key held in the window connects it to a row driven low now.
 * ================================================================ */
#include <string.h>
#include "../include/stm32f730.h"
#include "sim.h"

static GPIO_TypeDef s_porta, s_portc;
static volatile uint8_t s_down[KEY_COUNT];   /* set by the window thread */

static GPIO_TypeDef *columns(void);

#undef GPIOA
#define GPIOA (&s_porta)
#undef GPIOC
#define GPIOC (columns())
#include "../hal/keyboard.c"

/* The column inputs as they read now */
static GPIO_TypeDef *columns(void) {
    uint32_t idr = 0x3F;                         /* pull-ups: all high */
    uint32_t low = (s_porta.BSRR >> 16) & 0x1FF; /* rows the last BSRR write drove low */
    for (int r = 0; r < NROWS; r++) {
        if (!(low & (1U << ROW_PINS[r]))) continue;
        for (int c = 0; c < NCOLS; c++)
            if (s_matrix[r][c] != KEY_NONE && __atomic_load_n(&s_down[s_matrix[r][c]], __ATOMIC_RELAXED))
                idr &= ~(1U << COL_PINS[c]);
    }
    s_portc.IDR = idr;
    return &s_portc;
}

void sim_key(key_code_t k, bool down) {
    if (k > KEY_NONE && k < KEY_COUNT) __atomic_store_n(&s_down[k], (uint8_t)down, __ATOMIC_RELAXED);
}

bool sim_key_down(key_code_t k) {
    return k > KEY_NONE && k < KEY_COUNT && __atomic_load_n(&s_down[k], __ATOMIC_RELAXED);
}
