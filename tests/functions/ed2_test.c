/* Editor: 8 KB files, horizontal scrolling, rows that fit the screen */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/kernel.h"
#include "../../hal/keyboard.h"
void text_editor_init(void); void text_editor_handle_event(const kernel_event_t*); bool text_editor_open(const char*); void text_editor_redraw(void);
#include "../../hal/display.h"
extern const int et_top, et_char_h, et_char_w, et_text_x, et_cols, et_rows, et_footer_y, et_gutter;
extern const uint16_t et_keyword, et_comment, et_number, et_string;
static struct { char s[32]; uint16_t fg; } runs[64]; static int nruns;    /* text drawn, piece by piece */
static bool drawn(const char *s, uint16_t fg) { for (int i = 0; i < nruns; i++) if (!strcmp(runs[i].s, s) && runs[i].fg == fg) return true; return false; }
static char rows[32][64]; static int cursor_x = -1, cursor_y = -1; static char header_pos[32]; static char footer[128];
static int max_y_drawn = 0; static int left_mark = 0, right_mark = 0;
static bool in_text(int y) { return y >= et_top && y < et_footer_y; }
void display_fill(uint16_t c) {(void)c;}
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    (void)c; if (in_text(y) && y + h > max_y_drawn) max_y_drawn = y + h;
    if (w == 1 && h == et_char_h && in_text(y)) { cursor_x = x + 1 - et_text_x; cursor_y = (y - et_top) / et_char_h; }
    if (w == 1 && h == et_char_h - 4 && x == et_gutter + 1) left_mark++;
    if (w == 1 && h == et_char_h - 4 && x == 319) right_mark++;
}
void display_hline(int16_t x, int16_t y, int16_t w, uint16_t c) {(void)x;(void)y;(void)w;(void)c;}
void display_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {(void)x;(void)y;(void)w;(void)h;(void)c;}
/* Text: the code's rows (right of the line numbers), the footer's
 * message on the left and the cursor position on its right */
void display_text_n(int16_t x, int16_t y, const char *s, int n, const font_t *f, uint16_t a, uint16_t b) {
    (void)f;(void)a;(void)b;
    if (y >= et_footer_y) {
        if (x > 160) snprintf(header_pos, sizeof header_pos, "%.*s", n, s);
        else snprintf(footer, sizeof footer, "%.*s", n, s);
        return;
    }
    if (!in_text(y) || x < et_text_x) return;
    if (nruns < 64) { snprintf(runs[nruns].s, sizeof runs[nruns].s, "%.*s", n, s); runs[nruns++].fg = a; }
    int r = (y - et_top) / et_char_h, col = (x - et_text_x) / et_char_w;
    if (r < 32 && col + n < 64) { memcpy(rows[r] + col, s, (size_t)n); rows[r][col + n] = 0; }
    if (y + et_char_h > max_y_drawn) max_y_drawn = y + et_char_h;
}
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
    check(max_y_drawn <= et_footer_y, "text rows stop above the footer");
    for (int i = 0; i < 25; i++) ek(KEY_DOWN);
    check(cursor_y >= 0 && cursor_y < et_rows, "cursor stays on a visible row after scrolling down");
    check(!strcmp(header_pos, "r26 k1"), "footer shows row/column");
    for (int i = 0; i < 21; i++) ek(KEY_UP);       /* to line 5 (index 4) */
    check(!strcmp(header_pos, "r5 k1"), "back on the long line");
    for (int i = 0; i < 100; i++) ek(KEY_RIGHT);
    int r = cursor_y;
    int first = 101 - et_cols;                      /* the first column shown, from 0 */
    check(cursor_x >= 0 && et_text_x + cursor_x <= 319, "cursor visible at column 101");
    check((int)strlen(rows[r]) == et_cols && rows[r][0] == 'a' + first % 26 && rows[r][et_cols - 1] == 'a' + 100 % 26 &&
          cursor_x == (et_cols - 1) * et_char_w, "view scrolled right: the columns up to 101 shown, cursor on the last");
    check(left_mark > 0, "marker: more text to the left");
    check(right_mark > 0, "marker: more text to the right");
    check(!strcmp(header_pos, "r5 k101"), "column 101 in the footer");
    ek(KEY_DOWN);                                   /* short line: column clamps to 8 */
    check(cursor_x >= 0 && et_text_x + cursor_x <= 319 && !strcmp(header_pos, "r6 k9"), "short line below: scrolled back so cursor is visible");
    check(!strcmp(rows[cursor_y], "line 005"), "short line drawn from column 1");
    /* full file */
    ek(KEY_1);
    check(strstr(footer, "vol") != NULL, "typing into a full 8 KB file says so");
    /* a Python file in colour; another kind of file not */
    const char *py = "def f(x):  # hi\n    return 2*x + len('ab')\n";
    strcpy(fdata, py); flen = (uint32_t)strlen(py); strcpy(fname, "t.py");
    check(text_editor_open("t.py"), "a .py file opens");
    nruns = 0; text_editor_redraw();
    check(drawn("def", et_keyword) && drawn("return", et_keyword) && drawn("# hi", et_comment) &&
          drawn("2", et_number) && drawn("'ab'", et_string), "Python: keywords, comment, number, string in their colours");
    strcpy(fname, "t.txt");
    text_editor_open("t.txt"); nruns = 0; text_editor_redraw();
    check(drawn("def f(x):  # hi", 0), "a .txt file: each line in black");
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
