#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/kernel.h"
#include "../../hal/keyboard.h"
void shell_init(void); void shell_redraw(void); void shell_puts(const char*); void shell_handle_event(const kernel_event_t*);
/* display stubs: remember the text drawn on each row */
static char rows[300][128]; static int offscreen = 0, too_wide = 0;
void display_str(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg) {
    (void)fg; (void)bg;
    if (y < 0 || y + 8 > 240) offscreen++;
    if (x + (int)strlen(s) * 7 > 320 + 1) too_wide++;
    if (y >= 0 && y < 300) snprintf(rows[y], sizeof rows[y], "%s", s);
}
void display_fill(uint16_t c) { (void)c; memset(rows, 0, sizeof rows); }
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { (void)x;(void)w;(void)c; for (int i = y; i < y + h && i < 300; i++) if (i >= 0) rows[i][0] = 0; }
void hal_uart_puts(const char *s) { (void)s; }
int  hal_uart_getc(void) { return -1; }
void cmd_run(const char *l) { (void)l; }
app_state_t kernel_get_app(void) { return APP_SHELL; }
void kernel_request_redraw(void) {}
void kernel_set_app(app_state_t a) { (void)a; }
char key_to_char(key_code_t k, bool s, bool a) { (void)k;(void)s;(void)a; return 'x'; }
int main(void) {
    shell_init();
    char buf[32];
    for (int i = 0; i < 100; i++) { snprintf(buf, sizeof buf, "line %d\n", i); shell_puts(buf); }
    kernel_event_t ev = { .key = KEY_7, .action = 0 };
    for (int i = 0; i < 80; i++) shell_handle_event(&ev);   /* type a long command */
    shell_redraw();
    int last_y = -1; char last[128] = "";
    for (int y = 0; y < 240; y++) if (strncmp(rows[y], "line ", 5) == 0) { last_y = y; snprintf(last, sizeof last, "%s", rows[y]); }
    int input_y = -1; for (int y = 0; y < 300; y++) if (strstr(rows[y], "_")) input_y = y;
    printf("  last output line on screen: '%.10s' at y=%d\n", last, last_y);
    printf("  input line drawn at y=%d (screen is 240 high)\n", input_y);
    printf("  draws off-screen: %d, lines wider than the screen: %d\n", offscreen, too_wide);
    int fails = 0;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); else { printf("  FAIL %s\n", m); fails++; } } while (0)
    CHECK(strncmp(last, "line 99", 7) == 0 && strspn(last + 7, " ") == strlen(last + 7), "the newest output line is on screen");
    CHECK(input_y > last_y && input_y + 8 <= 240, "the input line sits below it, inside the screen");
    CHECK(offscreen == 0, "nothing is drawn off-screen");
    CHECK(too_wide == 0, "no line is wider than the screen");
    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails != 0;
}
bool line_input(char *buf, int max, void (*draw)(const char *, bool, bool)) { (void)buf; (void)max; (void)draw; return false; }
void display_flush(void) {}
