#pragma once
#include <stdint.h>
#include <stdbool.h>

/* NumWorks key codes */
typedef enum {
    KEY_NONE=0,
    KEY_LEFT,  KEY_RIGHT, KEY_UP,   KEY_DOWN,
    KEY_OK,    KEY_BACK,  KEY_HOME,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_DOT,  KEY_EE,    KEY_PLUS,  KEY_MINUS,
    KEY_MUL,  KEY_DIV,   KEY_POW,   KEY_SQRT,
    KEY_SIN,  KEY_COS,   KEY_TAN,   KEY_EXP,
    KEY_LN,   KEY_LOG,   KEY_A,     KEY_B,
    KEY_C,    KEY_D,     KEY_E,     KEY_F,
    KEY_G,    KEY_H,     KEY_I,     KEY_J,
    KEY_K,    KEY_L,     KEY_M,     KEY_N,
    KEY_O,    KEY_P,     KEY_Q,     KEY_R,
    KEY_S,    KEY_T,     KEY_U,     KEY_V,
    KEY_W,    KEY_X,     KEY_Y,     KEY_Z,
    KEY_SPACE, KEY_SHIFT, KEY_ALPHA, KEY_XNT,
    KEY_VAR,  KEY_TOOLBOX, KEY_BACKSPACE, KEY_EXE,
    KEY_COUNT
} key_code_t;

typedef struct {
    key_code_t key;
    uint8_t    action;   /* 0=press, 1=release */
} key_event_t;

void keyboard_init(void);
bool keyboard_poll(key_event_t *ev);  /* Returns true if event ready */
bool keyboard_is_pressed(key_code_t k);
char key_to_char(key_code_t k, bool shift, bool alpha);
