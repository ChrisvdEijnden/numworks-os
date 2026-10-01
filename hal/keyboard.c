/* ================================================================
 * NumWorks OS — Keyboard Driver
 * File: hal/keyboard.c
 *
 * NumWorks uses a 9×6 GPIO matrix.
 * Rows are driven low one-at-a-time (open-drain), columns are read.
 * Debounce: 2 identical reads, SCAN_PERIOD_MS apart = stable.
 * ================================================================ */
#include "keyboard.h"
#include "hal.h"
#include "uart.h"
#include "../include/stm32f730.h"
#include "../include/config.h"
#include "../include/string.h"
#include "../include/stdio.h"

/* Keyboard matrix GPIO mapping — NOT verified against a schematic (kept
 * from the original code); check before relying on it.
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

/* Map matrix (row,col) → key_code_t. This is NumWorks' own key order
 * (Epsilon's ion::Keyboard::Key, index = row*6 + column), i.e. the
 * physical layout read top-left to bottom-right. The GPIO pins above
 * still have to be checked against the board's schematic. */
static const key_code_t s_matrix[NROWS][NCOLS] = {
    {KEY_LEFT,  KEY_UP,     KEY_DOWN,    KEY_RIGHT,   KEY_OK,      KEY_BACK      },
    {KEY_HOME,  KEY_NONE,   KEY_ONOFF,   KEY_NONE,    KEY_NONE,    KEY_NONE      },
    {KEY_SHIFT, KEY_ALPHA,  KEY_XNT,     KEY_VAR,     KEY_TOOLBOX, KEY_BACKSPACE },
    {KEY_EXP,   KEY_LN,     KEY_LOG,     KEY_IMAG,    KEY_COMMA,   KEY_POW       },
    {KEY_SIN,   KEY_COS,    KEY_TAN,     KEY_PI,      KEY_SQRT,    KEY_SQUARE    },
    {KEY_7,     KEY_8,      KEY_9,       KEY_LPAREN,  KEY_RPAREN,  KEY_NONE      },
    {KEY_4,     KEY_5,      KEY_6,       KEY_MUL,     KEY_DIV,     KEY_NONE      },
    {KEY_1,     KEY_2,      KEY_3,       KEY_PLUS,    KEY_MINUS,   KEY_NONE      },
    {KEY_0,     KEY_DOT,    KEY_EE,      KEY_ANS,     KEY_EXE,     KEY_NONE      },
};

#define SCAN_PERIOD_MS  5
#define REPEAT_DELAY_MS 500   /* hold this long before a key repeats */
#define REPEAT_RATE_MS  100

static uint8_t s_state[NROWS][NCOLS];
static uint8_t s_debounce[NROWS][NCOLS];
static uint16_t s_row_ok;      /* bit r set: row r is ours to drive */
static uint8_t  s_col_ok;      /* bit c set: column c is ours to read */
static uint32_t s_last_scan;
static key_code_t s_rep_key = KEY_NONE;   /* held key that auto-repeats */
static uint32_t   s_rep_next;

/* Navigation and deletion repeat while held; typing keys don't */
static bool key_repeats(key_code_t k) {
    return k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN ||
           k == KEY_BACKSPACE;
}

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

static void report_skipped(const char *what, int idx) {
    char msg[64];
    snprintf(msg, sizeof(msg),
             "kbd: %s %d pin is owned by another peripheral, skipped\n", what, idx);
    hal_uart_puts(msg);
}

/* One pass over the matrix. With report=false the state is learned
 * without queueing events. */
static void keyboard_scan(bool report) {
    for (int r = 0; r < NROWS; r++) {
        if (!(s_row_ok & (1U << r))) continue;
        GPIO_TypeDef *rp = ROW_PORTS[r];
        uint32_t rpin = ROW_PINS[r];
        /* Drive row low, let the column lines settle (a few µs) */
        rp->BSRR = (1U << (rpin + 16));
        for (volatile int d = 0; d < 200; d++) {}
        uint32_t idr = COL_PORT->IDR;
        /* Release row */
        rp->BSRR = (1U << rpin);

        for (int c = 0; c < NCOLS; c++) {
            if (!(s_col_ok & (1U << c))) continue;
            uint8_t pressed = !((idr >> COL_PINS[c]) & 1);

            if (pressed == s_debounce[r][c]) {
                if (pressed != s_state[r][c]) {
                    s_state[r][c] = pressed;
                    key_code_t key = s_matrix[r][c];
                    if (report && key != KEY_NONE) {
                        kbuf_push(key, pressed ? 0 : 1);  /* 0=press, 1=release */
                        if (pressed && key_repeats(key)) {
                            s_rep_key  = key;
                            s_rep_next = hal_tick_ms() + REPEAT_DELAY_MS;
                        } else if (!pressed && key == s_rep_key) {
                            s_rep_key = KEY_NONE;
                        }
                    }
                }
            }
            s_debounce[r][c] = pressed;
        }
    }
}

void keyboard_init(void) {
    s_row_ok = 0;
    s_col_ok = 0;
    /* Rows: open-drain output, released (high-Z) when not scanned, so
     * two keys pressed in one column can't short two driven rows. Pins
     * that another peripheral already owns (e.g. the QSPI clock we are
     * executing from) are left alone. */
    for (int r = 0; r < NROWS; r++) {
        GPIO_TypeDef *p = ROW_PORTS[r];
        uint32_t pin = ROW_PINS[r];
        if (gpio_pin_is_af(p, pin)) { report_skipped("row", r); continue; }
        p->BSRR    = (1U << pin);                 /* released */
        p->OTYPER |= (1U << pin);                 /* open-drain */
        p->MODER   = (p->MODER & ~(3U << (pin*2))) | (1U << (pin*2));
        s_row_ok  |= (uint16_t)(1U << r);
    }
    /* Cols: input with pull-up */
    for (int c = 0; c < NCOLS; c++) {
        uint32_t pin = COL_PINS[c];
        if (gpio_pin_is_af(COL_PORT, pin)) { report_skipped("column", c); continue; }
        COL_PORT->MODER &= ~(3U << (pin*2));  /* input */
        COL_PORT->PUPDR &= ~(3U << (pin*2));
        COL_PORT->PUPDR |=  (1U << (pin*2));   /* pull-up */
        s_col_ok |= (uint8_t)(1U << c);
    }
    memset(s_state,    0, sizeof(s_state));
    memset(s_debounce, 0, sizeof(s_debounce));

    /* Learn which keys are already held (e.g. HOME at boot) without
     * reporting them as presses. */
    keyboard_scan(false);
    hal_delay_ms(SCAN_PERIOD_MS);
    keyboard_scan(false);
    s_last_scan = hal_tick_ms();
}

bool keyboard_poll(key_event_t *ev) {
    /* Scan on a fixed period so the 2-read debounce spans real time */
    uint32_t now = hal_tick_ms();
    if (now - s_last_scan >= SCAN_PERIOD_MS) {
        s_last_scan = now;
        keyboard_scan(true);
        /* Auto-repeat, delivered as further presses */
        if (s_rep_key != KEY_NONE && (int32_t)(now - s_rep_next) >= 0) {
            kbuf_push(s_rep_key, 0);
            s_rep_next = now + REPEAT_RATE_MS;
        }
    }
    if (s_khead == s_ktail) return false;
    *ev = s_kbuf[s_khead];
    s_khead = (s_khead + 1) % KBD_BUF;
    return true;
}

/* Whether any key is down right now, read straight from the matrix:
 * no debouncing, no events, no SysTick. For the crash screen. */
bool keyboard_raw_any(void) {
    for (int r = 0; r < NROWS; r++) {
        if (!(s_row_ok & (1U << r))) continue;
        GPIO_TypeDef *rp = ROW_PORTS[r];
        uint32_t rpin = ROW_PINS[r];
        rp->BSRR = (1U << (rpin + 16));
        for (volatile int d = 0; d < 200; d++) {}
        uint32_t idr = COL_PORT->IDR;
        rp->BSRR = (1U << rpin);
        for (int c = 0; c < NCOLS; c++)
            if ((s_col_ok & (1U << c)) && !((idr >> COL_PINS[c]) & 1)) return true;
    }
    return false;
}

bool keyboard_is_pressed(key_code_t k) {
    for (int r = 0; r < NROWS; r++)
        for (int c = 0; c < NCOLS; c++)
            if (s_matrix[r][c] == k && s_state[r][c]) return true;
    return false;
}

/* Convert key to ASCII character */
char key_to_char(key_code_t k, bool shift, bool alpha) {
    if (alpha) {
        /* Letters as printed on the NumWorks keys, a..z from e^x to + */
        static const struct { key_code_t key; char c; } ALPHA[] = {
            {KEY_EXP,'a'}, {KEY_LN,'b'},  {KEY_LOG,'c'},    {KEY_IMAG,'d'},
            {KEY_COMMA,'e'}, {KEY_POW,'f'}, {KEY_SIN,'g'},  {KEY_COS,'h'},
            {KEY_TAN,'i'}, {KEY_PI,'j'},  {KEY_SQRT,'k'},   {KEY_SQUARE,'l'},
            {KEY_7,'m'},   {KEY_8,'n'},   {KEY_9,'o'},      {KEY_LPAREN,'p'},
            {KEY_RPAREN,'q'}, {KEY_4,'r'}, {KEY_5,'s'},     {KEY_6,'t'},
            {KEY_MUL,'u'}, {KEY_DIV,'v'}, {KEY_1,'w'},      {KEY_2,'x'},
            {KEY_3,'y'},   {KEY_PLUS,'z'},
        };
        for (unsigned i = 0; i < sizeof(ALPHA) / sizeof(ALPHA[0]); i++)
            if (ALPHA[i].key == k)
                return shift ? (char)(ALPHA[i].c - 'a' + 'A') : ALPHA[i].c;
        switch (k) {
            case KEY_MINUS:   return ' ';
            case KEY_XNT:     return ':';
            case KEY_VAR:     return ';';
            case KEY_TOOLBOX: return '"';
            case KEY_0:       return '?';
            case KEY_DOT:     return '!';
            default:          return 0;
        }
    }
    if (shift) {
        switch (k) {
            case KEY_LPAREN: return '[';
            case KEY_RPAREN: return ']';
            case KEY_MUL:    return '{';
            case KEY_DIV:    return '}';
            case KEY_PLUS:   return '=';
            case KEY_MINUS:  return '_';
            case KEY_DOT:    return '<';
            case KEY_0:      return '>';
            case KEY_COMMA:  return '#';
            default:         break;   /* other keys type as unshifted */
        }
    }
    if (k >= KEY_0 && k <= KEY_9) return (char)('0' + (k - KEY_0));
    switch (k) {
        case KEY_PLUS:   return '+';
        case KEY_MINUS:  return '-';
        case KEY_MUL:    return '*';
        case KEY_DIV:    return '/';
        case KEY_DOT:    return '.';
        case KEY_COMMA:  return ',';
        case KEY_LPAREN: return '(';
        case KEY_RPAREN: return ')';
        case KEY_POW:    return '^';
        case KEY_EE:     return 'e';
        case KEY_XNT:    return 'x';
        default:         return 0;
    }
}
