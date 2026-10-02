/* Editor: 8 KB files, horizontal scrolling, rows that fit the screen */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/kernel.h"
#include "../../hal/keyboard.h"
void text_editor_init(void); void text_editor_handle_event(const kernel_event_t*); bool text_editor_open(const char*); void text_editor_redraw(void);
static char rows[32][64]; static int cursor_x = -1, cursor_y = -1; static char header_pos[32]; static char footer[128];
static int max_y_drawn = 0; static int left_mark = 0, right_mark = 0;
void display_fill(uint16_t c) {(void)c;}
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    (void)c; if (y >= 24 && y < 226) { if (y + h > max_y_drawn) max_y_drawn = y + h; }
    if (w == 2 && h == 10 && y >= 24 && y < 226) { cursor_x = x; cursor_y = (y - 24) / 10; }
    if (w == 1 && h == 6 && x == 0) left_mark++;
    if (w == 1 && h == 6 && x == 319) right_mark++;
}
void display_str(int16_t x, int16_t y, const char *s, uint16_t a, uint16_t b) {(void)a;(void)b;
    if (y >= 226) snprintf(footer, sizeof footer, "%s", s);
    else if (y == 6 && x > 160) snprintf(header_pos, sizeof header_pos, "%s", s); }
void display_str_len(int16_t x, int16_t y, const char *s, int len, uint16_t a, uint16_t b) {(void)x;(void)a;(void)b;
    int r = (y - 24) / 10; if (r >= 0 && r < 32) { snprintf(rows[r], sizeof rows[r], "%.*s", len, s); }
    if (y + 10 > max_y_drawn) max_y_drawn = y + 10; }
void kernel_set_app(app_state_t a) {(void)a;}
static char fdata[9000]; static uint32_t flen; static char fname[24];
bool flashfs_exists(const char *p) { return !strcmp(p, fname); }
int flashfs_write(const char *p, const void *d, uint32_t n) { strcpy(fname, p); memcpy(fdata, d, n); flen = n; return (int)n; }
int flashfs_open_read(const char *p, uint32_t *o, uint32_t *s) { if (strcmp(p, fname)) return -1; *o = 0; *s = flen; return 0; }
int flashfs_read(uint32_t o, void *b, uint32_t l) { memcpy(b, fdata + o, l); return (int)l; }
static int fails = 0;
static void check(int c, const char *w) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }
static void ek(key_code_t k) { memset(rows, 0, sizeof rows); left_mark = right_mark = 0; kernel_event_t e = { .key = k, .action = 0 }; text_editor_handle_event(&e); }
int main(void) {
    text_editor_init();
    /* 8 KB file: 100 lines, line 5 is 120 characters long */
    char *p = fdata;
    for (int i = 0; i < 100; i++) {
        if (i == 4) { for (int c = 0; c < 120; c++) *p++ = (char)('a' + c % 26); *p++ = '\n'; continue; }
        p += sprintf(p, "line %03d\n", i);
    }
    while (p - fdata < 8192) *p++ = '#';
    flen = (uint32_t)(p - fdata); strcpy(fname, "big.txt");
    check(flen == 8192, "test file is 8 KB");
    check(text_editor_open("big.txt"), "8 KB file opens (4 KB was the limit)");
    max_y_drawn = 0; text_editor_redraw();
    check(max_y_drawn <= 226, "text rows stop above the footer");
    for (int i = 0; i < 25; i++) ek(KEY_DOWN);
    check(cursor_y >= 0 && cursor_y < 20, "cursor stays on a visible row after scrolling down");
    check(!strcmp(header_pos, "r26 k1"), "header shows row/column");
    for (int i = 0; i < 21; i++) ek(KEY_UP);       /* to line 5 (index 4) */
    check(!strcmp(header_pos, "r5 k1"), "back on the long line");
    for (int i = 0; i < 100; i++) ek(KEY_RIGHT);
    int r = cursor_y;
    check(cursor_x >= 0 && cursor_x <= 318, "cursor visible at column 101");
    check(strlen(rows[r]) == 45 && rows[r][0] == 'a' + 56 % 26 && rows[r][44] == 'a' + 100 % 26 && cursor_x == 44 * 7, "view scrolled right: columns 57-101 shown, cursor on the last");
    check(left_mark > 0, "marker: more text to the left");
    check(right_mark > 0, "marker: more text to the right");
    check(!strcmp(header_pos, "r5 k101"), "column 101 in header");
    ek(KEY_DOWN);                                   /* short line: column clamps to 8 */
    check(cursor_x >= 0 && cursor_x <= 318 && !strcmp(header_pos, "r6 k9"), "short line below: scrolled back so cursor is visible");
    check(!strcmp(rows[cursor_y], "line 005"), "short line drawn from column 1");
    /* full file */
    ek(KEY_1);
    check(strstr(footer, "vol") != NULL, "typing into a full 8 KB file says so");
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
