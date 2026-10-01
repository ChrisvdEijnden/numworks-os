
/* ================================================================
 * NumWorks OS — Settings (Instellingen)
 * File: apps/settings/settings.c
 *
 * Settings:
 *  1. Lamp / LED: Uit | Rood | Groen | Blauw | Wit (once its pins are set)
 *  2. Taal: NL (fixed)
 *  3. Versie-info
 *  4. Systeem reset
 * ================================================================ */
#include "settings.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../hal/led.h"
#include "../../hal/fault.h"
#include "../../include/string.h"
#include "../../include/stdio.h"

#define C_BG   RGB(10,10,20)
#define C_HDR  RGB(60,60,80)
#define C_SEL  RGB(50,50,90)
#define C_BD   RGB(90,90,130)
#define HEADER_H 28
#define ROW_H    32

static led_colour_t s_lamp = LED_OFF;
static int s_cursor = 0;
#define N_SETTINGS 4

static const char *lamp_str(void) {
    static const char *NAMES[LED_COLOUR_COUNT] = { "Uit", "Rood", "Groen", "Blauw", "Wit" };
    return NAMES[s_lamp];
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
        case 0: {
            if (!led_available()) {
                display_str(10, y+8, "Lamp:  niet ingesteld (config.h)", GREY, bg);
                break;
            }
            char line[64];
            snprintf(line, sizeof(line), "Lamp:  %s", lamp_str());
            display_str(10, y+8, line, WHITE, bg);
            display_str(LCD_WIDTH-120, y+8, "L/R:Wissel", RGB(180,220,180), bg);
            display_fill_rect(LCD_WIDTH-30, y+6, 18, 18, lamp_swatch());
            break;
        }
        case 1:
            display_str(10, y+8, "Taal:  Nederlands", WHITE, bg);
            break;
        case 2:
            display_str(10, y+8, "Versie: NumWorks OS v" NWOS_VERSION, WHITE, bg);
            break;
        case 3:
            display_str(10, y+8, "Systeem herstarten", RED, bg);
            break;
    }
}

void settings_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0,0,LCD_WIDTH,HEADER_H,C_HDR);
    display_str(8, 8, "Instellingen", WHITE, C_HDR);
    display_str(LCD_WIDTH-66, 8, "HOME:Terug", RGB(200,200,220), C_HDR);
    for (int i=0; i<N_SETTINGS; i++) draw_row(i);
    display_str(4, LCD_HEIGHT-12,
                "UP/DOWN:Navigeren  L/R:Wijzigen  EXE:Bevestigen",
                YELLOW, C_BG);
}

void settings_init(void) { s_cursor=0; s_lamp=LED_OFF; }

void settings_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k==KEY_HOME||k==KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k==KEY_UP   && s_cursor>0)            { s_cursor--; settings_redraw(); return; }
    if (k==KEY_DOWN && s_cursor<N_SETTINGS-1) { s_cursor++; settings_redraw(); return; }

    if (s_cursor == 0) {  /* Lamp */
        if ((k==KEY_LEFT || k==KEY_RIGHT) && led_available()) {
            int n = LED_COLOUR_COUNT;
            int l = ((int)s_lamp + (k==KEY_RIGHT ? 1 : n-1)) % n;
            s_lamp = (led_colour_t)l;
            led_set(s_lamp);
            draw_row(0);
        }
    } else if (s_cursor == 3 && key_is_exe(k)) {
        hal_reset();
    }
}
