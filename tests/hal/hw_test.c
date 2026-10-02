/* Backlight pulse protocol, LED PWM set-up, battery levels */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fake_hw2.h"
#include "../../hal/backlight.h"
#include "../../hal/led.h"
#include "../../hal/battery.h"
#include "../../include/config.h"
GPIO_TypeDef fake_ports[5]; fake_rcc_t fake_rcc; TIM_TypeDef fake_tim3;
typedef struct { uint32_t SR, CR1, CR2, SMPR1, SMPR2, JOFR[4], HTR, LTR, SQR1, SQR2, SQR3, JSQR, JDR[4], DR; } adc_t;
adc_t fake_adc; uint32_t fake_adc_ccr, fake_otp = 0xFFFFFFFFu;
static uint64_t now_us;
void hal_delay_us(uint32_t us) { now_us += us; }

/* The backlight driver chip on PE0: off after a long low, on at the
 * brightest level on the next rising edge; a short low pulse steps one
 * level down, wrapping from 0 to 15. */
static int bl_on, bl_level, bl_high = 0; static uint64_t bl_edge; static uint64_t min_low = ~0ull;
void fake_pin_changed(int port, int pin, int level) {
    if (port != 4 || pin != 0) return;
    if (level && !bl_high) {
        uint64_t low = now_us - bl_edge;
        if (!bl_on || low >= 2500) { bl_on = 1; bl_level = 15; }
        else { bl_level = (bl_level + 15) % 16; if (low < min_low) min_low = low; }
    }
    if (!level && bl_high) bl_edge = now_us;
    if (!level) bl_high = 0; else bl_high = 1;
}
static void bl_tick(void) { if (bl_on && !bl_high && now_us - bl_edge >= 2500) bl_on = 0; }

static int fails;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); else { printf("  FAIL %s\n", m); fails++; } } while (0)

static led_charge_t last_charge = (led_charge_t)-1;
led_charge_t last_charge_hook(led_charge_t s) { return last_charge = s; }
int main(void) {
    puts("backlight:");
    fake_ports[4].ODR = 1; fake_ports[4].MODER = 1; bl_high = 1; bl_on = 1; bl_level = 4;   /* left on by the bootloader */
    backlight_init();
    bl_tick();
    CHECK(bl_on && bl_level == 12 && backlight_level() == 12, "init: switched off first, then on at the default level 12");
    int ok = 1;
    srand(3);
    for (int i = 0; i < 300; i++) {
        int l = rand() % 16;
        backlight_set_level((uint8_t)l);
        if (bl_level != l || backlight_level() != l) ok = 0;
    }
    CHECK(ok, "300 random level changes: the chip always ends on the level asked for");
    backlight_set_level(200);
    CHECK(bl_level == 15, "levels above 15 are clamped");
    CHECK(min_low >= 20, "step pulses are at least 20 us low");
    backlight_set_level(5);
    backlight_power(false); bl_tick();
    CHECK(!bl_on, "power off: the driver switches off");
    backlight_set_level(9);
    CHECK(!bl_on, "a level change while off doesn't switch it on");
    backlight_power(true);
    CHECK(bl_on && bl_level == 9, "power on: back at the level set meanwhile");

    puts("LED:");
    led_init();
    CHECK(fake_tim3.PSC == 0 && fake_tim3.ARR == 19999 && (fake_tim3.CR1 & 0x81) == 0x81, "TIM3: 96 MHz / 20000 = 4.8 kHz, preloaded, running");
    CHECK(fake_tim3.CCMR1 == ((6u << 4) | (1u << 3) | (6u << 12) | (1u << 11)) && fake_tim3.CCMR2 == ((6u << 4) | (1u << 3)),
          "channels 1-3 in PWM mode 1 with preload");
    CHECK(fake_tim3.CCER == 0x111, "outputs 1-3 enabled, active high");
    CHECK(((fake_ports[1].MODER >> 8) & 3) == 2 && ((fake_ports[1].AFR[0] >> 16) & 15) == 2 &&
          ((fake_ports[1].MODER >> 10) & 3) == 2 && ((fake_ports[1].AFR[0] >> 20) & 15) == 2 &&
          ((fake_ports[1].MODER >> 0) & 3) == 2 && ((fake_ports[1].AFR[0] >> 0) & 15) == 2, "PB4, PB5, PB0 on AF2 (TIM3)");
    CHECK(fake_tim3.CCR1 == 0 && fake_tim3.CCR2 == 0 && fake_tim3.CCR3 == 0, "off at start");
    led_set(LED_RED);
    CHECK(fake_tim3.CCR1 == 5000 && fake_tim3.CCR2 == 0 && fake_tim3.CCR3 == 0, "red: channel 1 at a quarter duty");
    led_set_charge(LED_CHARGE_CHARGING);
    CHECK(fake_tim3.CCR1 == 5000 && fake_tim3.CCR2 == 60u * 5000 / 255 && fake_tim3.CCR3 == 0, "charging shows orange over the chosen colour");
    led_suspend();
    CHECK(fake_tim3.CCR1 == 5000 && fake_tim3.CCR2 > 0, "asleep: the charge colour stays");
    led_set_charge(LED_CHARGE_NONE);
    CHECK(fake_tim3.CCR1 == 0 && fake_tim3.CCR2 == 0 && fake_tim3.CCR3 == 0, "asleep, unplugged: dark");
    led_resume();
    CHECK(fake_tim3.CCR1 == 5000 && fake_tim3.CCR2 == 0, "awake again: the chosen colour");
    led_set(LED_WHITE);
    CHECK(fake_tim3.CCR1 == 5000 && fake_tim3.CCR2 == 5000 && fake_tim3.CCR3 == 5000, "white: all three");

    puts("battery:");
    fake_otp = 0xFFFFFFFFu;                 /* blank OTP: PCB version 0 */
    fake_adc.DR = 2742;                     /* 3.75 V: 3750 / 2 / 2800 * 4095 */
    fake_ports[4].IDR = 1u << 3;            /* CHG released: not charging */
    fake_ports[0].IDR = 0;                  /* no VBUS */
    battery_init();
    CHECK(((fake_ports[0].MODER >> 18) & 3) == 2 && ((fake_ports[0].AFR[1] >> 4) & 15) == 10, "PCB version 0: PA9 on AF10 for VBUS");
    CHECK(((fake_ports[1].MODER >> 2) & 3) == 3 && ((fake_ports[4].PUPDR >> 6) & 3) == 1, "PB1 analog, PE3 pulled up");
    CHECK(fake_adc.SQR3 == 9 && ((fake_adc.SMPR2 >> 27) & 7) == 7 && ((fake_adc_ccr >> 16) & 3) == 1, "ADC: channel 9, 480 cycles, clock /4");
    uint32_t mv = battery_mv();
    printf("       code 2742 -> %u mV\n", mv);
    CHECK(mv >= 3748 && mv <= 3752, "voltage = 2 x 2.8 V x code / 4095");
    CHECK(battery_level() == BAT_MEDIUM, "3.75 V: medium (full from 3.8 V)");
    struct { int mv, want; } seq[] = {
        {3850, BAT_FULL}, {3790, BAT_FULL}, {3770, BAT_MEDIUM}, {3810, BAT_MEDIUM}, {3830, BAT_FULL},
        {3690, BAT_MEDIUM}, {3670, BAT_LOW}, {3610, BAT_LOW}, {3590, BAT_EMPTY}, {3630, BAT_EMPTY}, {3650, BAT_LOW},
    };
    battery_level_t lv = BAT_FULL; int hyst_ok = 1;
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        lv = battery_level_for((uint32_t)seq[i].mv, lv);
        if ((int)lv != seq[i].want) { printf("       %d mV -> %d, want %d\n", seq[i].mv, lv, seq[i].want); hyst_ok = 0; }
    }
    CHECK(hyst_ok, "thresholds 3.62/3.7/3.8 V with 20 mV hysteresis: no flicker at a boundary");
    fake_ports[0].IDR = 1u << 9;            /* VBUS */
    fake_ports[4].IDR = 0;                  /* CHG low: charging */
    bool changed = battery_poll();
    CHECK(changed && battery_usb_powered() && battery_charging() && last_charge == LED_CHARGE_CHARGING, "plugged in, charging: LED orange");
    fake_ports[4].IDR = 1u << 3;
    battery_poll();
    CHECK(last_charge == LED_CHARGE_FULL, "charger done: LED green");
    fake_ports[0].IDR = 0;
    battery_poll();
    CHECK(last_charge == LED_CHARGE_NONE, "unplugged: LED back to the chosen colour");
    memset(fake_ports, 0, sizeof fake_ports);
    fake_otp = ~1u;                         /* PCB version 1 */
    battery_init();
    CHECK(((fake_ports[0].MODER >> 18) & 3) == 0, "PCB version 1: PA9 a plain input");

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
