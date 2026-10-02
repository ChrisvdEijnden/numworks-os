/* Built with project headers: fake GPIO ports. Rows on port A (driven
 * low through BSRR), columns on port C; a column reads low when a
 * pressed key connects it to a row that is driven low right now. */
#include "../../include/stm32f730.h"
GPIO_TypeDef kfa, kfc;
int kpressed[9][6];
int col_reads;
static const int ROWPIN[9] = {1,0,2,3,4,5,6,7,8};   /* rows A..I */
static GPIO_TypeDef *portc(void) {
    uint32_t idr = 0x3F;                       /* pull-ups: all columns high */
    uint32_t low = (kfa.BSRR >> 16) & 0x1FF;  /* rows driven low by the last BSRR write */
    for (int r = 0; r < 9; r++)
        if (low & (1u << ROWPIN[r]))
            for (int c = 0; c < 6; c++) if (kpressed[r][c]) idr &= ~(1u << c);
    kfc.IDR = idr; col_reads++; return &kfc;
}
#undef GPIOA
#define GPIOA (&kfa)
#undef GPIOC
#define GPIOC (portc())
#include "../../hal/keyboard.c"
