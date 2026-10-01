#pragma once
#include <stdint.h>
#include <stdbool.h>

/* NumWorks key codes: the calculator's 46 physical keys. Letters are
 * not separate keys; they are the ALPHA function of other keys (see
 * key_to_char). */
typedef enum {
    KEY_NONE=0,
    KEY_LEFT,  KEY_RIGHT, KEY_UP,   KEY_DOWN,
    KEY_OK,    KEY_BACK,  KEY_HOME, KEY_ONOFF,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_DOT,  KEY_EE,    KEY_PLUS,  KEY_MINUS,
    KEY_MUL,  KEY_DIV,   KEY_POW,   KEY_SQRT,
    KEY_SIN,  KEY_COS,   KEY_TAN,   KEY_EXP,
    KEY_LN,   KEY_LOG,   KEY_IMAG,  KEY_COMMA,
    KEY_PI,   KEY_SQUARE, KEY_LPAREN, KEY_RPAREN,
    KEY_ANS,  KEY_SHIFT, KEY_ALPHA, KEY_XNT,
    KEY_VAR,  KEY_TOOLBOX, KEY_BACKSPACE, KEY_EXE,
    KEY_COUNT
} key_code_t;

typedef struct {
    key_code_t key;
    uint8_t    action;   /* 0=press (also auto-repeat), 1=release */
} key_event_t;

void keyboard_init(void);
bool keyboard_poll(key_event_t *ev);  /* Returns true if event ready */
bool keyboard_is_pressed(key_code_t k);

/* OK and EXE both confirm: always test for "execute" with this */
static inline bool key_is_exe(key_code_t k) { return k == KEY_EXE || k == KEY_OK; }
/* Character a key types in text fields (shell, editor, Python), or 0.
 * ALPHA: letters as printed on the keys (SHIFT+ALPHA: capitals).
 * SHIFT: [ ] { } = _ < > # on ( ) x / + - . 0 , */
char key_to_char(key_code_t k, bool shift, bool alpha);
