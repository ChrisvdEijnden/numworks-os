
/* ================================================================
 * NumWorks OS — Settings (Instellingen)
 * File: apps/settings/settings.c
 *
 * Settings:
 *  1. Lamp / LED: Uit | Rood | Groen | Blauw | Wit
 *  2. Helderheid (backlight, 16 levels)
 *  3. Taal / Language: Nederlands | English
 *  4. Versie-info
 *  5. Systeem reset
 * The values are kept in g_prefs (prefs.c) and saved to flash when
 * leaving the app, so they survive a restart.
 * ================================================================ */
#include "settings.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../hal/led.h"
#include "../../hal/backlight.h"
#include "../../ui/lang.h"
#include "../../ui/theme.h"
#include "prefs.h"
#include "../../hal/fault.h"
#include <string.h>
#include <stdio.h>

#define ROW_TOP  (UI_TITLE_H + 12)
#define ROW_STEP 38

static led_colour_t s_lamp = LED_OFF;
static int s_cursor = 0;
#define N_SETTINGS 5
enum { ROW_LAMP, ROW_BRIGHT, ROW_LANG, ROW_VERSION, ROW_RESET };

static const char *lamp_str(void) {
    static const char *const NL[LED_COLOUR_COUNT] = { "Uit", "Rood", "Groen", "Blauw", "Wit" };
    static const char *const EN[LED_COLOUR_COUNT] = { "Off", "Red", "Green", "Blue", "White" };
    return TR(NL[s_lamp], EN[s_lamp]);
}

static uint16_t lamp_swatch(void) {
    static const uint16_t SW[LED_COLOUR_COUNT] = { T_GRAY_MIDDLE, T_RED, T_GREEN, T_BLUE, WHITE };
    return SW[s_lamp];
}

static void draw_row(int i) {
    int16_t x = UI_MARGIN, w = LCD_WIDTH - 2 * UI_MARGIN, y = (int16_t)(ROW_TOP + i * ROW_STEP);
    bool sel = i == s_cursor;
    uint16_t bg = sel ? T_SELECT : WHITE;
    char value[40];
    /* values that LEFT/RIGHT change show arrows while selected */
    const char *fmt = sel ? "< %s >" : "%s";
    switch (i) {
        case ROW_LAMP:
            snprintf(value, sizeof value, fmt, lamp_str());
            ui_row(x, y, w, ROW_STEP + 1, TR("Lamp", "LED"), value, sel);
            display_fill_rect((int16_t)(x + w - 34 - display_text_width(value, &font_small)), (int16_t)(y + 13),
                              12, 12, lamp_swatch());
            display_rect((int16_t)(x + w - 34 - display_text_width(value, &font_small)), (int16_t)(y + 13),
                         12, 12, T_GRAY_DARK);
            break;
        case ROW_BRIGHT: {
            snprintf(value, sizeof value, "%d/%d", backlight_level() + 1, BACKLIGHT_MAX + 1);
            ui_row(x, y, w, ROW_STEP + 1, TR("Helderheid", "Brightness"), value, sel);
            int16_t bx = (int16_t)(x + w - 52 - (BACKLIGHT_MAX + 1) * 5);
            for (int l = 0; l <= BACKLIGHT_MAX; l++)
                display_fill_rect((int16_t)(bx + l * 5), (int16_t)(y + 27 - l), 4, (int16_t)(4 + l),
                                  l <= backlight_level() ? T_YELLOW : T_GRAY_MIDDLE);
            (void)bg;
            break;
        }
        case ROW_LANG:
            snprintf(value, sizeof value, fmt, lang_name(g_lang));
            ui_row(x, y, w, ROW_STEP + 1, TR("Taal", "Language"), value, sel);
            break;
        case ROW_VERSION:
            ui_row(x, y, w, ROW_STEP + 1, TR("Versie", "Version"), "NumWorks OS v" NWOS_VERSION, sel);
            break;
        case ROW_RESET:
            ui_row(x, y, w, ROW_STEP + 1, TR("Herstarten", "Restart"), sel ? "EXE" : NULL, sel);
            break;
    }
}

void settings_redraw(void) {
    ui_title_bar(TR("Instellingen", "Settings"));
    ui_body(T_WALL);
    for (int i = 0; i < N_SETTINGS; i++) draw_row(i);
}

void settings_init(void) { s_cursor=0; s_lamp=led_get(); }

void settings_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k==KEY_HOME||k==KEY_BACK) { prefs_save(); kernel_set_app(APP_HOME); return; }
    if (k==KEY_UP   && s_cursor>0)            { s_cursor--; draw_row(s_cursor + 1); draw_row(s_cursor); return; }
    if (k==KEY_DOWN && s_cursor<N_SETTINGS-1) { s_cursor++; draw_row(s_cursor - 1); draw_row(s_cursor); return; }

    if (s_cursor == ROW_LAMP) {
        if (k==KEY_LEFT || k==KEY_RIGHT) {
            int n = LED_COLOUR_COUNT;
            int l = ((int)s_lamp + (k==KEY_RIGHT ? 1 : n-1)) % n;
            s_lamp = (led_colour_t)l;
            led_set(s_lamp);
            g_prefs.led = (uint8_t)s_lamp;
            draw_row(ROW_LAMP);
        }
    } else if (s_cursor == ROW_BRIGHT) {
        int l = backlight_level();
        if (k==KEY_RIGHT && l < BACKLIGHT_MAX) backlight_set_level((uint8_t)(l + 1));
        if (k==KEY_LEFT  && l > 0)             backlight_set_level((uint8_t)(l - 1));
        g_prefs.brightness = backlight_level();
        draw_row(ROW_BRIGHT);
    } else if (s_cursor == ROW_LANG) {
        if (k==KEY_LEFT || k==KEY_RIGHT) {
            lang_set(g_lang == LANG_EN ? LANG_NL : LANG_EN);
            g_prefs.lang = (uint8_t)g_lang;
            settings_redraw();                 /* every text changes */
        }
    } else if (s_cursor == ROW_RESET && key_is_exe(k)) {
        prefs_save();
        hal_reset();
    }
}
