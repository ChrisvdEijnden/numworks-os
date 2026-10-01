/* ================================================================
 * NumWorks OS — settings kept across restarts
 * File: apps/settings/prefs.c
 *
 * A small text file, so it can be read (and fixed) from the PC:
 *   led=1
 *   brightness=12
 *   lang=en
 *   best_tetris=1200
 * Unknown keys and bad values are ignored; anything missing keeps its
 * default. The file is only rewritten when its contents change.
 * ================================================================ */
#include "prefs.h"
#include "../../fs/flashfs.h"
#include "../../hal/led.h"
#include "../../hal/backlight.h"
#include "../../ui/lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const prefs_t DEFAULTS = { LED_OFF, 12, LANG_NL, {0, 0, 0} };
static const char *const BEST_KEYS[BEST_COUNT] = { "best_tetris", "best_snake", "best_2048" };

prefs_t g_prefs = { LED_OFF, 12, LANG_NL, {0, 0, 0} };

int prefs_format(const prefs_t *p, char *out, int max) {
    int n = snprintf(out, (size_t)max, "led=%u\nbrightness=%u\nlang=%s\n",
                     p->led, p->brightness, p->lang == LANG_EN ? "en" : "nl");
    for (int i = 0; i < BEST_COUNT && n < max; i++)
        n += snprintf(out + n, (size_t)(max - n), "%s=%lu\n", BEST_KEYS[i], (unsigned long)p->best[i]);
    return n < max ? n : max - 1;
}

static bool number(const char *s, unsigned long max, unsigned long *v) {
    char *end;
    if (*s < '0' || *s > '9') return false;
    unsigned long x = strtoul(s, &end, 10);
    if (*end || x > max) return false;
    *v = x;
    return true;
}

void prefs_parse(prefs_t *p, const char *text, uint32_t len) {
    *p = DEFAULTS;
    char line[48];
    uint32_t i = 0;
    while (i < len) {
        uint32_t n = 0;
        while (i < len && text[i] != '\n') {
            if (n < sizeof(line) - 1 && text[i] != '\r') line[n++] = text[i];
            i++;
        }
        i++;
        line[n] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *key = line, *val = eq + 1;
        unsigned long v;
        if (!strcmp(key, "led") && number(val, LED_COLOUR_COUNT - 1, &v)) p->led = (uint8_t)v;
        else if (!strcmp(key, "brightness") && number(val, BACKLIGHT_MAX, &v)) p->brightness = (uint8_t)v;
        else if (!strcmp(key, "lang")) {
            if (!strcmp(val, "en")) p->lang = LANG_EN;
            else if (!strcmp(val, "nl")) p->lang = LANG_NL;
        } else {
            for (int b = 0; b < BEST_COUNT; b++)
                if (!strcmp(key, BEST_KEYS[b]) && number(val, 0xFFFFFFFFUL, &v)) p->best[b] = (uint32_t)v;
        }
    }
}

void prefs_apply(void) {
    led_set((led_colour_t)g_prefs.led);
    backlight_set_level(g_prefs.brightness);
    lang_set((lang_t)g_prefs.lang);
}

void prefs_load(void) {
    const char *data;
    uint32_t size;
    if (flashfs_mounted() && flashfs_map(PREFS_FILE, &data, &size)) prefs_parse(&g_prefs, data, size);
    else g_prefs = DEFAULTS;
    prefs_apply();
}

bool prefs_save(void) {
    if (!flashfs_mounted()) return false;
    char text[192];
    int n = prefs_format(&g_prefs, text, sizeof(text));
    const char *old;
    uint32_t size;
    if (flashfs_map(PREFS_FILE, &old, &size) && size == (uint32_t)n && memcmp(old, text, (size_t)n) == 0)
        return true;                                   /* unchanged: no flash write */
    return flashfs_write(PREFS_FILE, text, (uint32_t)n) == n;
}

bool prefs_new_best(best_t game, uint32_t score) {
    if (game >= BEST_COUNT || score <= g_prefs.best[game]) return false;
    g_prefs.best[game] = score;
    prefs_save();
    return true;
}
