
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
#include "prefs.h"
#include "../../hal/fault.h"
#include <string.h>
#include <stdio.h>

#define C_BG   RGB(10,10,20)
#define C_HDR  RGB(60,60,80)
#define C_SEL  RGB(50,50,90)
#define C_BD   RGB(90,90,130)
#define HEADER_H 28
#define ROW_H    32

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
    static const uint16_t SW[LED_COLOUR_COUNT] = { RGB(40,40,40), RED, GREEN, BLUE, WHITE };
    return SW[s_lamp];
}

static void draw_row(int i) {
    int y = HEADER_H + 10 + i*ROW_H;
    bool sel = (i == s_cursor);
    uint16_t bg = sel ? C_SEL : C_BG;
    uint16_t bd = sel ? RGB(140,140,200) : C_BD;
    display_fill_rect(4, y, LCD_WIDTH-8, ROW_H-4, bg);
    display_rect     (4, y, LCD_WIDTH-8, ROW_H-4, bd);

    switch (i) {
        case ROW_LAMP: {
            char line[64];
            snprintf(line, sizeof(line), TR("Lamp:  %s", "LED:   %s"), lamp_str());
            display_str(10, y+8, line, WHITE, bg);
            display_str(LCD_WIDTH-120, y+8, TR("L/R:Wissel", "L/R:Change"), RGB(180,220,180), bg);
            display_fill_rect(LCD_WIDTH-30, y+6, 18, 18, lamp_swatch());
            break;
        }
        case ROW_BRIGHT: {
            char line[32];
            snprintf(line, sizeof(line), TR("Helderheid: %2d/%d", "Brightness: %2d/%d"),
                     backlight_level() + 1, BACKLIGHT_MAX + 1);
            display_str(10, y+8, line, WHITE, bg);
            int bx = LCD_WIDTH - 12 - (BACKLIGHT_MAX + 1) * 5;
            for (int l = 0; l <= BACKLIGHT_MAX; l++)
                display_fill_rect(bx + l*5, y+20-l, 4, 4+l,
                                  l <= backlight_level() ? YELLOW : RGB(60,60,70));
            break;
        }
        case ROW_LANG: {
            char line[40];
            snprintf(line, sizeof(line), TR("Taal:  %s", "Language: %s"), lang_name(g_lang));
            display_str(10, y+8, line, WHITE, bg);
            display_str(LCD_WIDTH-120, y+8, TR("L/R:Wissel", "L/R:Change"), RGB(180,220,180), bg);
            break;
        }
        case ROW_VERSION:
            display_str(10, y+8, TR("Versie: NumWorks OS v" NWOS_VERSION,
                                    "Version: NumWorks OS v" NWOS_VERSION), WHITE, bg);
            break;
        case ROW_RESET:
            display_str(10, y+8, TR("Systeem herstarten", "Restart"), RED, bg);
            break;
    }
}

void settings_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0,0,LCD_WIDTH,HEADER_H,C_HDR);
    display_str(8, 8, TR("Instellingen", "Settings"), WHITE, C_HDR);
    display_str(LCD_WIDTH-82, 8, TR("HOME:Terug", "HOME:Back"), RGB(200,200,220), C_HDR);
    for (int i=0; i<N_SETTINGS; i++) draw_row(i);
    display_str(4, LCD_HEIGHT-12,
                TR("UP/DOWN:Kies  L/R:Wijzig  EXE:Bevestig",
                   "UP/DOWN:Select  L/R:Change  EXE:Confirm"),
                YELLOW, C_BG);
}

void settings_init(void) { s_cursor=0; s_lamp=led_get(); }

void settings_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k==KEY_HOME||k==KEY_BACK) { prefs_save(); kernel_set_app(APP_HOME); return; }
    if (k==KEY_UP   && s_cursor>0)            { s_cursor--; settings_redraw(); return; }
    if (k==KEY_DOWN && s_cursor<N_SETTINGS-1) { s_cursor++; settings_redraw(); return; }

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
