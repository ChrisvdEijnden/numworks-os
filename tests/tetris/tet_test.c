#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/kernel.h"
#include "../../hal/keyboard.h"
#include "../../hal/display.h"
void tetris_init(void); void tetris_redraw(void); void tetris_tick(void); void tetris_handle_event(const kernel_event_t *ev);
int tt_py(void); int tt_px(void); void tt_set_level(int); void tt_clear_lines(void); unsigned tt_drop_ms(void);
static uint16_t fb[240][320]; static uint32_t now = 1000;
extern const uint16_t tt_bg; extern const int tt_ox, tt_oy;
void display_pixel(int16_t x, int16_t y, uint16_t c) { if (x >= 0 && x < 320 && y >= 0 && y < 240) fb[y][x] = c; }
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) display_pixel(i, j, c); }
void display_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { for (int i = x; i < x + w; i++) { display_pixel(i, y, c); display_pixel(i, y + h - 1, c); } for (int j = y; j < y + h; j++) { display_pixel(x, j, c); display_pixel(x + w - 1, j, c); } }
void display_fill(uint16_t c) { for (int j = 0; j < 240; j++) for (int i = 0; i < 320; i++) fb[j][i] = c; }
void display_text_n(int16_t x, int16_t y, const char *s, int n, const font_t *f, uint16_t a, uint16_t b) { (void)x;(void)y;(void)s;(void)n;(void)f;(void)a;(void)b; }
void display_hline(int16_t x, int16_t y, int16_t w, uint16_t c) { display_fill_rect(x, y, w, 1, c); }
uint32_t hal_tick_ms(void) { return now; }
uint32_t hal_tick_us(void) { return now * 1000u; }
void kernel_set_app(app_state_t a) { (void)a; }
static int fails = 0;
static void check(int c, const char *w) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }
static void key(key_code_t k) { kernel_event_t ev = { .key = k, .action = 0 }; tetris_handle_event(&ev); }
/* count non-background pixels inside the 10x20 board area */
static int board_pixels(void) { int n = 0; for (int y = tt_oy; y < tt_oy + 200; y++) for (int x = tt_ox; x < tt_ox + 100; x++) if (fb[y][x] != tt_bg) n++; return n; }
int main(void) {
    tetris_init(); tetris_redraw();
    int y0 = tt_py(); now += 599; tetris_tick();
    check(tt_py() == y0, "no drop before 600 ms");
    now += 1; tetris_tick();
    check(tt_py() == y0 + 1, "piece falls on its own after 600 ms (no key pressed)");
    int before = board_pixels();
    key(KEY_LEFT); key(KEY_LEFT); key(KEY_RIGHT);
    check(board_pixels() == before, "moving leaves no trail (same lit-pixel count)");
    key(KEY_7);
    check(board_pixels() == before, "unhandled key keeps the piece visible");
    tt_set_level(16); tt_clear_lines();
    check(tt_drop_ms() == 100, "level 16: drop interval clamps to 100 ms (was ~49 days)");
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
/* saved best score (apps/settings/prefs.c) */
#include "../../apps/settings/prefs.h"
prefs_t g_prefs;
bool prefs_new_best(best_t g, uint32_t s) { if (s <= g_prefs.best[g]) return false; g_prefs.best[g] = s; return true; }
