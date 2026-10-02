#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../../hal/keyboard.h"
#include "../../kernel/kernel.h"
#include "../../include/config.h"
volatile uint32_t g_tick_ms = 1000, g_tick_step = 1;
void SysTick_Handler(void);
static int lcd_on = 1, power_calls = 0, home_redraws = 0, usb_calls = 0, delays = 0;
static uint32_t period = 1;
static int backlight = 1, bl_on_before_lcd = 0, usb_power = 1, low_clock = 0, clocks_low_calls = 0, polls = 0, usb_while_low = 0;
void display_power(bool on) { lcd_on = on; power_calls++; }
void hal_tick_set_period(uint32_t ms) { period = ms; g_tick_step = ms; }
void hal_delay_ms(uint32_t ms) { delays++; uint32_t s = g_tick_ms; while (g_tick_ms - s < ms) SysTick_Handler(); }
void hal_uart_puts(const char *s) { (void)s; }
void usb_cdc_process(void) { usb_calls++; if (low_clock) usb_while_low++; }
void home_redraw(void) { home_redraws++; }
void backlight_power(bool on) { if (on && !lcd_on) bl_on_before_lcd++; backlight = on; }
bool battery_usb_powered(void) { return usb_power; }
bool battery_poll(void) { polls++; return false; }
void clocks_low(void) { low_clock = 1; clocks_low_calls++; }
void clocks_high(void) { low_clock = 0; }
/* scripted keyboard: events become available at given tick times */
typedef struct { uint32_t at; uint8_t key, act; } kev_t;
static kev_t script[16]; static int nscript, pos;
bool keyboard_poll(key_event_t *ev) {
    if (pos < nscript && g_tick_ms >= script[pos].at) { ev->key = script[pos].key; ev->action = script[pos].act; pos++; return true; }
    return false;
}
/* scheduler stubs */
void scheduler_init(void){} void scheduler_tick(void){} void scheduler_sleep(uint32_t t){(void)t;}
void scheduler_add_task(const char*n, void(*f)(void), int p){(void)n;(void)f;(void)p;}
bool scheduler_ready_above(int p){(void)p;return true;} void scheduler_run_next(void){}
static int fails;
#define CHECK(c,w) do{ if(!(c)){printf("FAIL %s\n",w);fails++;} else printf("ok   %s\n",w);}while(0)
static void advance(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) { SysTick_Handler(); task_input(); } }
int main(void) {
    kernel_init();
    extern void kernel_set_app(app_state_t); kernel_set_app(APP_HOME);
    /* kernel_run() sets s_last_input; emulate by a key press at t=1000 */
    script[nscript++] = (kev_t){1000, KEY_OK, 0};
    script[nscript++] = (kev_t){1005, KEY_OK, 1};
    script[nscript++] = (kev_t){2000, KEY_ONOFF, 0};      /* sleep */
    script[nscript++] = (kev_t){2100, KEY_ONOFF, 1};
    script[nscript++] = (kev_t){2500, KEY_5, 0};          /* ignored while asleep */
    script[nscript++] = (kev_t){3000, KEY_ONOFF, 0};      /* wake */
    advance(10);
    kernel_event_t e; int n = 0; while (kernel_event_get(&e)) n++;
    CHECK(n == 2, "normal keys reach the app");
    uint32_t t0 = g_tick_ms;
    advance(1000);   /* reaches 2000 -> sleeps inside task_input until 3000 */
    CHECK(power_calls == 2 && lcd_on, "ON/OFF turned the screen off and on again");
    CHECK(backlight == 1 && bl_on_before_lcd == 0, "backlight on again after waking, once the panel is on");
    CHECK(clocks_low_calls == 0, "on USB power the clocks stay up (USB needs them)");
    CHECK(g_tick_ms >= 3000, "stayed asleep until the second ON/OFF");
    CHECK(period == 1 && g_tick_step == 1, "1 ms tick restored after waking");
    CHECK(usb_calls > 0, "PC transfer serviced while asleep");
    n = 0; int five = 0; while (kernel_event_get(&e)) { n++; if (e.key == KEY_5) five = 1; }
    CHECK(!five, "keys pressed while asleep don't reach the app");
    CHECK(delays <= (3000 - 2000) / 20 + 5, "slow tick while asleep: ~50 wake-ups per second");
    /* the redraw request is honoured by task_shell; here check it was requested by calling it */
    task_shell();
    CHECK(home_redraws >= 2, "app redrawn after waking");
    /* auto sleep: no keys for AUTO_SLEEP_MS */
    script[nscript++] = (kev_t){g_tick_ms + AUTO_SLEEP_MS + 5000, KEY_ONOFF, 0};
    int before = power_calls; uint32_t start = g_tick_ms;
    usb_power = 0; usb_calls = 0; polls = 0;
    advance(AUTO_SLEEP_MS + 10);
    CHECK(clocks_low_calls == 1 && !low_clock, "on battery: 16 MHz while asleep, full speed after waking");
    CHECK(usb_while_low == 0, "no USB work while the clocks are low");
    CHECK(polls >= 2, "battery polled every 2 s while asleep");
    CHECK(power_calls == before + 2, "auto sleep after 5 minutes without keys, woken by ON/OFF");
    CHECK(g_tick_ms - start >= AUTO_SLEEP_MS + 5000, "slept until ON/OFF");
    (void)t0;
    printf(fails ? "FAILURES %d\n" : "ALL PASS\n", fails); return fails != 0;
}
