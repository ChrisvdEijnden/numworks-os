/* Calculator history, statistics, Snake, 2048, Games menu, saved
 * settings and the language switch, against the real file system on
 * simulated flash. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "../../kernel/kernel.h"
#include "../../hal/keyboard.h"
#include "../../hal/led.h"
#include "../../fs/flashfs.h"
#include "../../ui/lang.h"
#include "../../apps/calculator/calculator.h"
#include "../../apps/statistics/statistics.h"
#include "../../apps/common/stats.h"
#include "../../apps/games/games.h"
#include "../../apps/settings/prefs.h"
#include "../../apps/settings/settings.h"
#include "../../hal/display.h"

/* ── stubs ── */
#define SHOWN 256
static char shown[SHOWN][80]; static int nshown;
static void log_str(const char *s, int n) { snprintf(shown[nshown % SHOWN], 80, "%.*s", n, s); nshown++; }
static bool was_shown(const char *needle) {
    for (int i = 0; i < SHOWN && i < nshown; i++) if (strstr(shown[i], needle)) return true;
    return false;
}
static void clear_shown(void) { nshown = 0; memset(shown, 0, sizeof shown); }
void display_text_n(int16_t x, int16_t y, const char *s, int n, const font_t *f, uint16_t a, uint16_t b) {
    (void)x;(void)y;(void)f;(void)a;(void)b; log_str(s, n); }
void display_fill(uint16_t c) { (void)c; }
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void display_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void display_pixel(int16_t x, int16_t y, uint16_t c) { (void)x;(void)y;(void)c; }
void display_hline(int16_t x, int16_t y, int16_t w, uint16_t c) { (void)x;(void)y;(void)w;(void)c; }
void display_vline(int16_t x, int16_t y, int16_t h, uint16_t c) { (void)x;(void)y;(void)h;(void)c; }
static app_state_t last_app = APP_COUNT;
void kernel_set_app(app_state_t a) { last_app = a; }
static uint32_t now_ms = 5000;
uint32_t hal_tick_us(void) { return now_ms * 1000u + 123; }
void hal_reset(void) {}
static led_colour_t led_now; static uint8_t bl_now = 15;
void led_set(led_colour_t c) { led_now = c; }
led_colour_t led_get(void) { return led_now; }
void backlight_set_level(uint8_t l) { bl_now = l; }
uint8_t backlight_level(void) { return bl_now; }

static int fails;
static void check(int c, const char *w) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }
static void key(void (*h)(const kernel_event_t *), key_code_t k) { kernel_event_t e = { .key = k, .action = 0 }; h(&e); }
static void ck(key_code_t k) { key(calculator_handle_event, k); }
static void sk(key_code_t k) { key(statistics_handle_event, k); }
static bool near(double a, double b) { return fabs(a - b) < 1e-9 * (1 + fabs(b)); }
static int file_is(const char *name, const char *want) {
    const char *d; uint32_t n;
    return flashfs_map(name, &d, &n) && n == strlen(want) && !memcmp(d, want, n);
}

int main(void) {
    flashfs_init(); flashfs_format();

    puts("calculator history:");
    calculator_init();
    ck(KEY_2); ck(KEY_PLUS); ck(KEY_3); ck(KEY_EXE);
    check(calculator_history_count() == 1 && !strcmp(calculator_history_expr(0), "2+3") &&
          !strcmp(calculator_history_result(0), "5") && !strcmp(calculator_input(), ""), "2+3 EXE: in the history as 2+3 = 5, input cleared");
    ck(KEY_ANS); ck(KEY_MUL); ck(KEY_2); ck(KEY_EXE);
    check(!strcmp(calculator_history_result(1), "10"), "ans*2 = 10");
    clear_shown();
    ck(KEY_2); ck(KEY_PLUS); ck(KEY_EXE);
    check(calculator_history_count() == 2 && !strcmp(calculator_input(), "2+") && was_shown("Fout: syntaxfout"),
          "an error keeps the input for fixing, nothing added");
    ck(KEY_BACKSPACE); ck(KEY_BACKSPACE);
    ck(KEY_UP);                                        /* newest: result 10 */
    ck(KEY_EXE);
    check(!strcmp(calculator_input(), "10"), "UP + OK: the newest result goes into the input");
    ck(KEY_UP); ck(KEY_UP); ck(KEY_LEFT); ck(KEY_EXE);  /* older one's calculation */
    check(!strcmp(calculator_input(), "102+3"), "UP UP LEFT OK: an older calculation appended");
    for (int i = 0; i < 5; i++) ck(KEY_BACKSPACE);
    ck(KEY_UP); ck(KEY_UP); ck(KEY_BACKSPACE);
    check(calculator_history_count() == 1 && !strcmp(calculator_history_expr(0), "ans*2"), "BACKSPACE on a selected entry removes it");
    ck(KEY_DOWN); ck(KEY_DOWN);
    for (int i = 0; i < 25; i++) { ck(KEY_1); ck(KEY_EXE); }
    check(calculator_history_count() == CALC_HISTORY && !strcmp(calculator_history_expr(0), "1"), "25 more: the 20 newest kept, oldest dropped");
    for (int i = 0; i < 60; i++) { ck(KEY_1); ck(KEY_PLUS); }
    ck(KEY_1); ck(KEY_EXE);
    const char *last = calculator_history_expr(calculator_history_count() - 1);
    check(strlen(last) == 121 && !strcmp(calculator_history_result(calculator_history_count() - 1), "61"),
          "a 121-character calculation kept whole (reusing it gives the same result)");
    lang_set(LANG_EN); clear_shown();
    ck(KEY_LPAREN); ck(KEY_EXE);
    check(was_shown("Error: unbalanced brackets") || was_shown("Error: syntax error"), "errors in English");
    lang_set(LANG_NL);

    puts("statistics maths:");
    stats1_t s;
    const double d1[] = { 2, 4, 4, 4, 5, 5, 7, 9 };
    stats1(d1, 8, &s);
    check(s.n == 8 && near(s.sum, 40) && near(s.mean, 5) && near(s.median, 4.5) && near(s.q1, 4) && near(s.q3, 6),
          "2 4 4 4 5 5 7 9: sum 40, mean 5, median 4.5, Q1 4, Q3 6");
    check(near(s.sd_pop, 2) && near(s.sd_sample, sqrt(32.0 / 7)) && near(s.min, 2) && near(s.max, 9) && near(s.range, 7),
          "sd 2 (population), 2.138 (sample); min 2 max 9 range 7");
    const double d2[] = { 7, 1, 5, 3, 6, 2, 4 };
    stats1(d2, 7, &s);
    check(near(s.median, 4) && near(s.q1, 2) && near(s.q3, 6), "odd n, unsorted: median 4, Q1 2, Q3 6 (median left out, as on the TI-84)");
    const double one = 3.5;
    check(stats1(&one, 1, &s) && near(s.q1, 3.5) && near(s.q3, 3.5) && s.sd_sample == 0, "one value: quartiles = the value, sample sd 0");
    check(!stats1(d1, 0, &s), "no values: refused");
    linreg_t lr;
    const double x3[] = { 1, 2, 3 }, y3[] = { 2, 4, 6 }, y4[] = { 3, 5, 4 }, yc[] = { 7, 7, 7 }, xc[] = { 2, 2, 2 };
    check(stats_linreg(x3, y3, 3, &lr) && near(lr.a, 2) && fabs(lr.b) < 1e-12 && near(lr.r, 1), "(1,2) (2,4) (3,6): y = 2x, r = 1");
    check(stats_linreg(x3, y4, 3, &lr) && near(lr.a, 0.5) && near(lr.b, 3) && near(lr.r, 0.5) && near(lr.r2, 0.25), "(1,3) (2,5) (3,4): y = 0.5x + 3, r = 0.5");
    check(stats_linreg(x3, yc, 3, &lr) && near(lr.a, 0) && isnan(lr.r), "all y equal: flat line, r undefined (not 1)");
    check(!stats_linreg(xc, y3, 3, &lr) && !stats_linreg(x3, y3, 1, &lr), "all x equal or one point: no line");

    puts("statistics app:");
    statistics_init();
    sk(KEY_1); sk(KEY_EXE); sk(KEY_2); sk(KEY_EXE); sk(KEY_3); sk(KEY_EXE);
    check(statistics_rows() == 3 && statistics_cell(2, 0) == 3 && isnan(statistics_cell(0, 1)), "X: 1 2 3 typed, each OK goes down");
    sk(KEY_RIGHT); sk(KEY_UP); sk(KEY_UP); sk(KEY_UP);
    sk(KEY_2); sk(KEY_EXE); sk(KEY_4); sk(KEY_EXE); sk(KEY_1); sk(KEY_DIV); sk(KEY_4); sk(KEY_EXE);
    check(statistics_cell(0, 1) == 2 && statistics_cell(1, 1) == 4 && statistics_cell(2, 1) == 0.25, "Y: 2 4 1/4 (expressions allowed)");
    clear_shown();
    sk(KEY_1); sk(KEY_DIV); sk(KEY_EXE);
    check(statistics_rows() == 3 && was_shown("syntaxfout"), "a bad entry is refused with a message, no row added");
    sk(KEY_BACK);                                        /* cancel the edit */
    sk(KEY_UP); sk(KEY_BACKSPACE);                       /* clear Y of row 3 */
    sk(KEY_LEFT); sk(KEY_BACKSPACE);                     /* and X: row removed */
    check(statistics_rows() == 2, "clearing both cells of a row removes it");
    statistics_save();
    check(file_is(STATS_FILE, "1,2\n2,4\n"), "saved as stats.csv: 1,2 / 2,4");
    statistics_set_text("", 0);
    statistics_init();
    check(statistics_rows() == 2 && statistics_cell(1, 1) == 4, "read back from stats.csv");
    const char *pc = "10,\r\n\r\n,5\n2.5e1,3\nabc\n";
    statistics_set_text(pc, (unsigned)strlen(pc));
    check(statistics_rows() == 3 && statistics_cell(0, 0) == 10 && isnan(statistics_cell(0, 1)) && isnan(statistics_cell(1, 0)) &&
          statistics_cell(2, 0) == 25, "a CSV from the PC: CRLF, empty cells, 2.5e1; blank and junk lines skipped");
    sk(KEY_HOME);
    check(last_app == APP_HOME, "HOME saves and leaves");

    puts("2048 rules:");
    uint8_t b[4][4];
    #define ROW(r, a0, a1, a2, a3) (b[r][0] = a0, b[r][1] = a1, b[r][2] = a2, b[r][3] = a3)
    memset(b, 0, sizeof b); ROW(0, 1, 1, 1, 1);
    int p = g2048_slide(b, DIR_LEFT);
    check(p == 8 && b[0][0] == 2 && b[0][1] == 2 && b[0][2] == 0, "2 2 2 2 left: 4 4, 8 points");
    memset(b, 0, sizeof b); ROW(0, 1, 0, 1, 2);
    p = g2048_slide(b, DIR_LEFT);
    check(p == 4 && b[0][0] == 2 && b[0][1] == 2 && b[0][2] == 0, "2 . 2 4 left: 4 4 (the new 4 doesn't merge again)");
    memset(b, 0, sizeof b); ROW(0, 2, 2, 3, 0);
    p = g2048_slide(b, DIR_LEFT);
    check(p == 8 && b[0][0] == 3 && b[0][1] == 3, "4 4 8 left: 8 8, not 16");
    memset(b, 0, sizeof b); ROW(0, 1, 1, 1, 0);
    p = g2048_slide(b, DIR_RIGHT);
    check(p == 4 && b[0][3] == 2 && b[0][2] == 1 && b[0][1] == 0, "2 2 2 . right: . . 2 4 (the far pair merges)");
    memset(b, 0, sizeof b); b[0][1] = 1; b[3][1] = 1;
    p = g2048_slide(b, DIR_DOWN);
    check(p == 4 && b[3][1] == 2 && b[0][1] == 0, "a column slides down and merges");
    memset(b, 0, sizeof b); ROW(0, 1, 2, 3, 4);
    check(g2048_slide(b, DIR_LEFT) == -1, "nothing moves: -1 (no new tile)");
    for (int r = 0; r < 4; r++) ROW(r, 1 + (r % 2), 2 - (r % 2), 1 + (r % 2), 2 - (r % 2));
    check(!g2048_can_move(b), "full board, no equal neighbours: game over");
    b[3][3] = b[3][2];
    check(g2048_can_move(b), "one equal pair: still a move");

    puts("snake:");
    snake_init();
    int snake_length(void); bool snake_over(void); int snake_head(void); void snake_put_apple(int); void snake_step(void);
    key(snake_handle_event, KEY_RIGHT);
    int h = snake_head();
    snake_put_apple(h + 1);
    snake_step();
    check(snake_length() == 4 && snake_head() == h + 1, "eating an apple: one longer");
    key(snake_handle_event, KEY_LEFT);
    snake_step();
    check(snake_head() == h + 2, "LEFT while going right is ignored (no turning back on yourself)");
    for (int i = 0; i < 5; i++) { snake_put_apple(snake_head() + 1); snake_step(); }   /* length 9 */
    key(snake_handle_event, KEY_UP); snake_step();
    key(snake_handle_event, KEY_LEFT); snake_step();
    key(snake_handle_event, KEY_DOWN); snake_step();
    check(snake_over(), "turning back into its own body: game over");
    check(g_prefs.best[BEST_SNAKE] == 60, "score 60 (6 apples) recorded as the best");
    snake_init();
    key(snake_handle_event, KEY_RIGHT);
    for (int i = 0; i < SNAKE_W && !snake_over(); i++) snake_step();
    check(snake_over() && g_prefs.best[BEST_SNAKE] == 60, "into the wall: game over; a lower score keeps the record");

    puts("games menu:");
    games_init();
    key(games_handle_event, KEY_EXE);
    check(last_app == APP_TETRIS, "OK on the first entry: Tetris");
    key(games_handle_event, KEY_DOWN); key(games_handle_event, KEY_EXE);
    check(last_app == APP_SNAKE, "DOWN OK: Snake");
    key(games_handle_event, KEY_DOWN); key(games_handle_event, KEY_EXE);
    check(last_app == APP_2048, "DOWN OK: 2048");
    clear_shown(); games_redraw();
    check(was_shown("Record: 60"), "the menu shows the best scores");

    puts("saved settings and language:");
    prefs_t a = { LED_BLUE, 7, LANG_EN, { 1200, 60, 2048 } }, c;
    char text[200];
    int n = prefs_format(&a, text, sizeof text);
    prefs_parse(&c, text, (uint32_t)n);
    check(a.led == c.led && a.brightness == c.brightness && a.lang == c.lang &&    /* not memcmp: */
          !memcmp(a.best, c.best, sizeof a.best),                                /* padding */
          "settings survive a round trip through the text file");
    const char *junk = "led=99\r\nbrightness=3\r\nlang=fr\nwho=me\nbest_snake=abc\nbest_2048=128\n";
    prefs_parse(&c, junk, (uint32_t)strlen(junk));
    check(c.led == LED_OFF && c.brightness == 3 && c.lang == LANG_NL && c.best[BEST_SNAKE] == 0 && c.best[BEST_2048] == 128,
          "bad values and unknown keys ignored, CRLF fine, defaults for the rest");
    settings_init();
    void settings_handle_event(const kernel_event_t *);
    key(settings_handle_event, KEY_RIGHT);                 /* LED: red */
    key(settings_handle_event, KEY_DOWN); key(settings_handle_event, KEY_LEFT); key(settings_handle_event, KEY_LEFT);
    key(settings_handle_event, KEY_DOWN); key(settings_handle_event, KEY_RIGHT);     /* language */
    check(g_lang == LANG_EN && led_now == LED_RED && bl_now == 13, "Settings: LED red, brightness 14/16, language English");
    clear_shown(); settings_redraw();
    check(was_shown("SETTINGS") && was_shown("Language") && was_shown("English") && was_shown("Red"),
          "the screen is in English at once");
    key(settings_handle_event, KEY_HOME);
    const char *d; uint32_t sz;
    check(flashfs_map(PREFS_FILE, &d, &sz) && memmem(d, sz, "led=1\n", 6) && memmem(d, sz, "brightness=13\n", 14) &&
          memmem(d, sz, "lang=en\n", 8) && memmem(d, sz, "best_snake=60\n", 14), "leaving Settings writes .settings");
    uint32_t off1, off2;
    flashfs_open_read(PREFS_FILE, &off1, &sz);
    prefs_save();
    flashfs_open_read(PREFS_FILE, &off2, &sz);
    check(off1 == off2, "saving unchanged settings doesn't write the flash again");
    led_now = LED_OFF; bl_now = 15; lang_set(LANG_NL);
    flashfs_init();                                        /* a restart */
    prefs_load();
    check(led_now == LED_RED && bl_now == 13 && g_lang == LANG_EN && g_prefs.best[BEST_SNAKE] == 60, "after a restart: all back as they were");
    lang_set(LANG_NL);

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
