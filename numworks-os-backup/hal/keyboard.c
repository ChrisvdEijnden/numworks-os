/* ================================================================
 * NumWorks OS — Keyboard Driver
 * File: hal/keyboard.c
 *
 * NumWorks uses a 9×6 GPIO matrix.
 * Rows are driven one-at-a-time, columns are read.
 * Debounce: 2 consecutive identical reads = stable.
 * ================================================================ */
#include "keyboard.h"
#include "../include/stm32f730.h"
#include "../include/config.h"
#include <string.h>

/* NumWorks N0110 keyboard matrix GPIO mapping
 * Rows:  PC0 PC1 PC2 PC3 PC4 PC5 PB0 PB1 PB2
 * Cols:  PA0 PA1 PA2 PA3 PA4 PA5  */
#define NROWS KEY_ROWS
#define NCOLS KEY_COLS

static const uint32_t ROW_PINS[NROWS] = {0,1,2,3,4,5,0,1,2};  /* pin number */
static GPIO_TypeDef * const ROW_PORTS[NROWS] = {
    GPIOC,GPIOC,GPIOC,GPIOC,GPIOC,GPIOC,GPIOB,GPIOB,GPIOB
};
static const uint32_t COL_PINS[NCOLS] = {0,1,2,3,4,5};
#define COL_PORT GPIOA

/* Map matrix (row,col) → key_code_t */
static const key_code_t s_matrix[NROWS][NCOLS] = {
    {KEY_LEFT,   KEY_UP,    KEY_DOWN,   KEY_RIGHT,  KEY_OK,      KEY_BACK  },
    {KEY_HOME,   KEY_XNT,   KEY_VAR,    KEY_TOOLBOX,KEY_SHIFT,   KEY_ALPHA },
    {KEY_EXP,    KEY_LN,    KEY_LOG,    KEY_SQRT,   KEY_SIN,     KEY_COS   },
    {KEY_TAN,    KEY_P,    KEY_POW,    KEY_7,      KEY_8,       KEY_9     },
    {KEY_A,      KEY_B,     KEY_C,      KEY_4,      KEY_5,       KEY_6     },
    {KEY_D,      KEY_E,     KEY_F,      KEY_1,      KEY_2,       KEY_3     },
    {KEY_G,      KEY_H,     KEY_I,      KEY_0,      KEY_DOT,     KEY_EE    },
    {KEY_J,      KEY_K,     KEY_L,      KEY_BACKSPACE,KEY_SPACE, KEY_PLUS  },
    {KEY_M,      KEY_N,     KEY_O,      KEY_MINUS,  KEY_MUL,     KEY_DIV   },
};

static uint8_t s_state[NROWS][NCOLS];
static uint8_t s_debounce[NROWS][NCOLS];

/* Ring buffer for key events */
#define KBD_BUF 16
static key_event_t s_kbuf[KBD_BUF];
static uint8_t s_khead = 0, s_ktail = 0;

static void kbuf_push(key_code_t k, uint8_t act) {
    uint8_t next = (s_ktail + 1) % KBD_BUF;
    if (next != s_khead) {
        s_kbuf[s_ktail].key    = k;
        s_kbuf[s_ktail].action = act;
        s_ktail = next;
    }
}

void keyboard_init(void) {
    /* Rows: push-pull output */
    for (int r = 0; r < NROWS; r++) {
        GPIO_TypeDef *p = ROW_PORTS[r];
        uint32_t pin = ROW_PINS[r];
        p->MODER &= ~(3U << (pin*2));
        p->MODER |=  (1U << (pin*2));  /* output */
        p->BSRR   = (1U << (pin+16));  /* low */
    }
    /* Cols: input with pull-up */
    for (int c = 0; c < NCOLS; c++) {
        uint32_t pin = COL_PINS[c];
        COL_PORT->MODER &= ~(3U << (pin*2));  /* input */
        COL_PORT->PUPDR &= ~(3U << (pin*2));
        COL_PORT->PUPDR |=  (1U << (pin*2));   /* pull-up */
    }
    memset(s_state,    0, sizeof(s_state));
    memset(s_debounce, 0, sizeof(s_debounce));
}

/* Call this regularly (e.g. every 10 ms) */
static void keyboard_scan(void) {
    for (int r = 0; r < NROWS; r++) {
        GPIO_TypeDef *rp = ROW_PORTS[r];
        uint32_t rpin = ROW_PINS[r];
        /* Drive row low */
        rp->BSRR = (1U << (rpin + 16));
        for (volatile int d = 0; d < 20; d++) {}

        for (int c = 0; c < NCOLS; c++) {
            uint8_t pressed = !((COL_PORT->IDR >> COL_PINS[c]) & 1);

            if (pressed == s_debounce[r][c]) {
                if (pressed != s_state[r][c]) {
                    s_state[r][c] = pressed;
                    if (s_matrix[r][c] != KEY_NONE)
                        kbuf_push(s_matrix[r][c], pressed ? 0 : 1);
                }
            }
            s_debounce[r][c] = pressed;
        }
        /* Release row */
        rp->BSRR = (1U << rpin);
    }
}

bool keyboard_poll(key_event_t *ev) {
    keyboard_scan();
    if (s_khead == s_ktail) return false;
    *ev = s_kbuf[s_khead];
    s_khead = (s_khead + 1) % KBD_BUF;
    return true;
}

bool keyboard_is_pressed(key_code_t k) {
    for (int r = 0; r < NROWS; r++)
        for (int c = 0; c < NCOLS; c++)
            if (s_matrix[r][c] == k && s_state[r][c]) return true;
    return false;
}

/* Convert key to ASCII character */
char key_to_char(key_code_t k, bool shift, bool alpha) {
    static const char normal[] = "0123456789.+-*/^";
    static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (k >= KEY_0 && k <= KEY_9) {
        char c = '0' + (k - KEY_0);
        return alpha ? 0 : c;
    }
    if (k == KEY_PLUS)  return '+';
    if (k == KEY_MINUS) return '-';
    if (k == KEY_MUL)   return '*';
    if (k == KEY_DIV)   return '/';
    if (k == KEY_DOT)   return '.';
    if (k == KEY_SPACE) return ' ';
    if (k == KEY_A && alpha) return shift ? 'A' : 'a';
    if (k >= KEY_A && k <= KEY_Z) {
        char base = alpha ? (shift ? 'A' : 'a') : 0;
        return base ? (char)(base + (k - KEY_A)) : 0;
    }
    (void)normal; (void)letters;
    return 0;
}
