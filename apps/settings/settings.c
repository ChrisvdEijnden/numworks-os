
/* ================================================================
 * NumWorks OS — Settings (Instellingen)
 * File: apps/settings/settings.c
 *
 * Settings:
 *  1. Lamp / LED: Rood | Wit | Uit
 *  2. Taal: NL (fixed)
 *  3. Versie-info
 *  4. Systeem reset
 * ================================================================ */
#include "settings.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../include/stm32f730.h"
#include "../../include/string.h"
#include "../../include/stdio.h"

#define C_BG   RGB(10,10,20)
#define C_HDR  RGB(60,60,80)
#define C_SEL  RGB(50,50,90)
#define C_BD   RGB(90,90,130)
#define HEADER_H 28
#define ROW_H    32

typedef enum { LAMP_OFF, LAMP_RED, LAMP_WHITE } lamp_state_t;
static lamp_state_t s_lamp = LAMP_OFF;
static int s_cursor = 0;
#define N_SETTINGS 4

/* ── LED control via TIM1 PWM on PE3 ─────────────────────────── */
static void led_set(lamp_state_t state) {
    /* Enable GPIOE clock */
    RCC->AHB1ENR |= (1U << 4);  /* GPIOEEN */
    volatile uint32_t *moder = (volatile uint32_t *)0x40021000UL; /* GPIOE MODER */

    switch (state) {
        case LAMP_OFF:
            /* PE3 output low */
            *moder = (*moder & ~(3U<<6)) | (1U<<6);   /* output */
            *(volatile uint32_t *)0x40021014UL &= ~(1U<<3);  /* GPIOE BSRR reset */
            break;
        case LAMP_RED:
            /* PE3 output high (red channel only — hardware specific) */
            *moder = (*moder & ~(3U<<6)) | (1U<<6);
            *(volatile uint32_t *)0x40021018UL |= (1U<<3);   /* ODR set */
            break;
        case LAMP_WHITE:
            /* Max brightness */
            *moder = (*moder & ~(3U<<6)) | (1U<<6);
            *(volatile uint32_t *)0x40021018UL |= (1U<<3);
            /* Full duty — simplified, real HW would use PWM */
            break;
    }
    s_lamp = state;
}

static const char *lamp_str(void) {
    switch(s_lamp) {
        case LAMP_OFF:   return "Uit";
        case LAMP_RED:   return "Rood";
        case LAMP_WHITE: return "Wit";
    }
    return "Uit";
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
            char line[64];
            snprintf(line, sizeof(line), "Lamp:  %s", lamp_str());
            display_str(10, y+8, line, WHITE, bg);
            display_str(LCD_WIDTH-68, y+8, "L/R:Wisselen", RGB(180,220,180), bg);
            /* Colour swatch */
            uint16_t sc = (s_lamp==LAMP_OFF)?RGB(40,40,40):(s_lamp==LAMP_RED)?RED:WHITE;
            display_fill_rect(LCD_WIDTH-30, y+6, 18, 18, sc);
            break;
        }
        case 1:
            display_str(10, y+8, "Taal:  Nederlands", WHITE, bg);
            break;
        case 2:
            display_str(10, y+8, "Versie: NumWorks OS v1.0 N0120", WHITE, bg);
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

void settings_init(void) { s_cursor=0; s_lamp=LAMP_OFF; }

void settings_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k==KEY_HOME||k==KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k==KEY_UP   && s_cursor>0)            { s_cursor--; settings_redraw(); return; }
    if (k==KEY_DOWN && s_cursor<N_SETTINGS-1) { s_cursor++; settings_redraw(); return; }

    if (s_cursor == 0) {  /* Lamp */
        if (k==KEY_LEFT || k==KEY_RIGHT) {
            int l = (int)s_lamp;
            if (k==KEY_RIGHT) l=(l+1)%3; else l=(l+2)%3;
            led_set((lamp_state_t)l);
            draw_row(0);
        }
    } else if (s_cursor == 3 && k == KEY_EXE) {
        /* Reboot */
        volatile uint32_t *aircr = (volatile uint32_t *)0xE000ED0CUL;
        *aircr = (0x5FAUL << 16) | (1U << 2);
        while(1){}
    }
}
