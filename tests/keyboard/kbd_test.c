#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../hal/keyboard.h"
extern int kpressed[9][6];
typedef struct { volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2]; } G;
extern G kfa; extern int col_reads;
static uint32_t now = 0; static int uart_msgs = 0;
uint32_t hal_tick_ms(void) { return now; }
void hal_delay_ms(uint32_t ms) { now += ms; }
void hal_uart_puts(const char *s) { (void)s; uart_msgs++; }
void hal_delay_us(uint32_t us) { (void)us; }
static int fails = 0;
static void check(int c, const char *w) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }
static int drain(key_code_t k, int action) { key_event_t ev; int n = 0; while (keyboard_poll(&ev)) if (ev.key == k && ev.action == action) n++; return n; }
static int run_ms(int ms, key_code_t k) { int n = 0; for (int i = 0; i < ms; i++) { now++; n += drain(k, 0); } return n; }
int main(void) {
    kpressed[1][0] = 1;                        /* HOME held at power-on */
    kfa.MODER |= 2u << (8 * 2);                /* PA8 (row I) already in AF mode */
    keyboard_init();
    check(keyboard_is_pressed(KEY_HOME), "HOME held at boot is seen by keyboard_is_pressed");
    check(run_ms(20, KEY_HOME) == 0, "... and is not reported as a key press");
    check(((kfa.MODER >> 16) & 3u) == 2u && uart_msgs == 1, "PA8 (row I) left in AF mode, one warning logged");
    check(((kfa.MODER >> 2) & 3u) == 1u && ((kfa.OTYPER >> 1) & 1u) && ((kfa.MODER >> 0) & 3u) == 1u,
          "PA1 (row A) and PA0 (row B) are open-drain outputs");
    kpressed[1][0] = 0; run_ms(20, KEY_NONE);

    { int r0 = col_reads; run_ms(100, KEY_NONE);
      check(col_reads - r0 == 20, "idle: one column read per 5 ms scan"); }
    kpressed[1][2] = 1; check(run_ms(15, KEY_ONOFF) == 1, "row B (PA0) col 3 is ON/OFF");
    kpressed[1][2] = 0; run_ms(20, KEY_NONE);
    kpressed[0][5] = 1; check(run_ms(15, KEY_BACK) == 1, "row A (PA1) col 6 is BACK");
    kpressed[0][5] = 0; run_ms(20, KEY_NONE);
    kpressed[8][4] = 1; check(run_ms(30, KEY_EXE) == 0, "row I is skipped (its pin was taken)");
    kpressed[8][4] = 0; run_ms(20, KEY_NONE);
    kpressed[5][3] = 1; check(run_ms(15, KEY_LPAREN) == 1, "row F col 4 is the ( key: one press");
    { int r0 = col_reads; run_ms(100, KEY_NONE);
      printf("       held: %d column reads in 100 ms\n", col_reads - r0); check(col_reads - r0 == 20 * 8, "a key held: each usable row (8) read per scan, no idle check"); }
    check(run_ms(1000, KEY_LPAREN) == 0, "( does not auto-repeat");
    kpressed[5][3] = 0; run_ms(20, KEY_NONE);

    for (int i = 0; i < 40; i++) { kpressed[0][0] = (i & 1); now += 1; drain(KEY_LEFT, 0); }   /* 1 ms chatter */
    kpressed[0][0] = 0;
    int bounced = run_ms(20, KEY_LEFT);
    check(bounced == 0, "1 ms contact chatter produces no press");

    kpressed[0][0] = 1;
    int first = run_ms(10, KEY_LEFT), early = run_ms(480, KEY_LEFT), reps = run_ms(1010, KEY_LEFT);
    kpressed[0][0] = 0; int after = run_ms(500, KEY_LEFT);
    printf("       LEFT held 1.5 s: %d press, %d before 0.5 s, %d repeats, %d after release\n", first, early, reps, after);
    check(first == 1 && early == 0 && reps >= 9 && reps <= 11 && after == 0, "LEFT repeats every ~100 ms after 500 ms, stops on release");

    check(key_to_char(KEY_EXP, false, true) == 'a' && key_to_char(KEY_PLUS, false, true) == 'z' &&
          key_to_char(KEY_MINUS, false, true) == ' ' && key_to_char(KEY_SIN, true, true) == 'G', "ALPHA letters a..z, space, SHIFT+ALPHA capitals");
    check(key_to_char(KEY_PLUS, true, false) == '=' && key_to_char(KEY_LPAREN, false, false) == '(' &&
          key_to_char(KEY_MINUS, true, false) == '_' && key_to_char(KEY_7, true, false) == '7', "SHIFT symbols = _ and plain ( 7");
    check(key_to_char(KEY_EXP, true, false) == '[' && key_to_char(KEY_LN, true, false) == ']' &&
          key_to_char(KEY_LOG, true, false) == '{' && key_to_char(KEY_IMAG, true, false) == '}' &&
          key_to_char(KEY_COMMA, true, false) == '_' && key_to_char(KEY_PI, true, false) == '=' &&
          key_to_char(KEY_SQRT, true, false) == '<' && key_to_char(KEY_SQUARE, true, false) == '>',
          "SHIFT types what is printed in orange on the keys");
    check(key_to_char(KEY_0, true, false) == '#' && key_to_char(KEY_DOT, true, false) == '%' &&
          key_to_char(KEY_XNT, false, true) == ':' && key_to_char(KEY_TOOLBOX, false, true) == '"',
          "and # %, ALPHA's : and \" for Python");
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
