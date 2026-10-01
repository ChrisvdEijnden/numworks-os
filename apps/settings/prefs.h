#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Settings kept across restarts, in the text file PREFS_FILE:
 * one "key=value" per line. */
#define PREFS_FILE ".settings"

typedef enum { BEST_TETRIS, BEST_SNAKE, BEST_2048, BEST_COUNT } best_t;

typedef struct {
    uint8_t  led;            /* led_colour_t */
    uint8_t  brightness;     /* 0..BACKLIGHT_MAX */
    uint8_t  lang;           /* lang_t */
    uint32_t best[BEST_COUNT];
} prefs_t;

extern prefs_t g_prefs;

void prefs_load(void);       /* read the file (defaults if missing) and apply */
bool prefs_save(void);       /* write it if it changed; false if that failed */
void prefs_apply(void);      /* LED, backlight and language from g_prefs */
/* A game's score: true (and saved) if it beats the best so far */
bool prefs_new_best(best_t game, uint32_t score);

/* For tests: text <-> settings */
int  prefs_format(const prefs_t *p, char *out, int max);
void prefs_parse(prefs_t *p, const char *text, uint32_t len);
