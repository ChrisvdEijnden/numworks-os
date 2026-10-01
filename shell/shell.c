/* ================================================================
 * NumWorks OS — Minimal Shell
 * File: shell/shell.c
 *
 * Renders on the LCD display.
 * Input: keypad (alpha+shift modes for text entry)
 * Also accepts characters from UART for debug.
 *
 * Screen layout (320×240, 6×8 font, 1px spacing):
 *   40 chars wide
 *   Top bar: header
 *   20 rows of scrolling output (bottom row = line being printed)
 *   Input line "> _" along the bottom edge
 *
 * Code size target: < 5 KB
 * ================================================================ */
#include "shell.h"
#include "commands.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../hal/uart.h"
#include "../hal/font.h"
#include "../include/config.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include <stdarg.h>

#define COLS    40
#define CHAR_W  (FONT_W + 1)
#define CHAR_H  (FONT_H + 2)
#define BAR_H   18
#define INPUT_H (CHAR_H + 4)
#define INPUT_Y (LCD_HEIGHT - INPUT_H + 2)          /* text baseline of input */
#define ROWS    ((LCD_HEIGHT - BAR_H - INPUT_H) / CHAR_H)   /* 20 */
/* Input characters that fit after the "> " prompt and before the "_" */
#define INPUT_VISIBLE (((LCD_WIDTH - 2) / CHAR_W) - 3)

/* Scrollback ring: line n lives in s_lines[n % ROWS] */
static char  s_lines[ROWS][COLS+1];
static int   s_nlines = 0;
static char  s_input[SHELL_LINE_LEN+1];
static int   s_inlen = 0;
static bool  s_shift = false;
static bool  s_alpha = false;
static bool  s_dirty = true;

/* History ring */
static char  s_hist[SHELL_HISTORY][SHELL_LINE_LEN+1];
static int   s_hist_head = 0, s_hist_cnt = 0, s_hist_pos = -1;

/* ── Rendering ───────────────────────────────────────────────── */
static void draw_header(void) {
    display_fill_rect(0, 0, LCD_WIDTH, BAR_H, BLUE);
    display_str(4, 4, "NumWorks OS  Shell", WHITE, BLUE);
}

static void draw_output(void) {
    /* Show the last ROWS lines, ending with the line being printed */
    int first = s_nlines - (ROWS - 1);
    for (int r = 0; r < ROWS; r++) {
        int li = first + r;
        int y  = BAR_H + r * CHAR_H;
        if (li >= 0) {
            /* Pad to clear old content */
            char padded[COLS+1];
            snprintf(padded, sizeof(padded), "%-*s", COLS, s_lines[li % ROWS]);
            display_str(0, y, padded, GREEN, BLACK);
        } else {
            display_fill_rect(0, y, LCD_WIDTH, CHAR_H, BLACK);
        }
    }
}

static void draw_input(void) {
    display_fill_rect(0, INPUT_Y - 2, LCD_WIDTH, INPUT_H, DKGREY);
    char prompt[SHELL_LINE_LEN + 4];
    char mode = s_alpha ? (s_shift ? 'A' : 'a') : (s_shift ? '^' : '>');
    /* Long input scrolls: show its tail so the cursor stays on screen */
    const char *tail = s_input;
    if (s_inlen > INPUT_VISIBLE) tail += s_inlen - INPUT_VISIBLE;
    snprintf(prompt, sizeof(prompt), "%c %s_", mode, tail);
    display_str(2, INPUT_Y, prompt, YELLOW, DKGREY);
}

void shell_redraw(void) {
    display_fill(BLACK);
    draw_header();
    draw_output();
    draw_input();
    s_dirty = false;
}

/* ── Output ──────────────────────────────────────────────────── */
void shell_puts(const char *s) {
    /* Mirror to UART first, before we consume the pointer */
    hal_uart_puts(s);
    /* Split on newlines, append to scrollback */
    while (*s) {
        int row = s_nlines % ROWS;
        int col = (int)strlen(s_lines[row]);
        while (*s && *s != '\n' && col < COLS) {
            s_lines[row][col++] = *s++;
            s_lines[row][col]   = 0;
        }
        if (*s == '\n' || col >= COLS) {
            s_nlines++;
            /* wrap, keeping s_nlines % ROWS so the ring stays in order */
            if (s_nlines > 1000000) s_nlines = ROWS + s_nlines % ROWS;
            int nr = s_nlines % ROWS;
            memset(s_lines[nr], 0, COLS+1);
            if (*s == '\n') s++;
        }
    }
    s_dirty = true;
    /* Output can arrive from anywhere (commands, Python, file manager):
     * make sure it reaches the screen if the shell is showing. */
    if (kernel_get_app() == APP_SHELL) kernel_request_redraw();
}

void shell_putc(char c) {
    char buf[2] = {c, 0};
    shell_puts(buf);
}

void shell_print(const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    shell_puts(buf);
}

/* ── Input handling ──────────────────────────────────────────── */
static void execute(void) {
    if (!s_inlen) return;
    /* Echo command */
    shell_print("> %s\n", s_input);
    /* Save to history */
    strncpy(s_hist[s_hist_head % SHELL_HISTORY], s_input, SHELL_LINE_LEN);
    s_hist_head = (s_hist_head + 1) % SHELL_HISTORY;
    if (s_hist_cnt < SHELL_HISTORY) s_hist_cnt++;
    /* Run command */
    cmd_run(s_input);
    /* Clear input */
    memset(s_input, 0, sizeof(s_input));
    s_inlen   = 0;
    s_hist_pos = -1;
    s_dirty   = true;
}

/* Redraw what changed, unless a command switched to another app */
static void refresh(void) {
    if (!s_dirty || kernel_get_app() != APP_SHELL) return;
    draw_output();
    draw_input();
    s_dirty = false;
}

void shell_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;  /* Only handle key presses */

    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) { kernel_set_app(APP_HOME); return; }

    /* Mode keys */
    if (k == KEY_SHIFT) { s_shift = !s_shift; s_dirty = true; refresh(); return; }
    if (k == KEY_ALPHA) { s_alpha = !s_alpha; s_dirty = true; refresh(); return; }

    /* Navigation */
    if (k == KEY_EXE || k == KEY_OK) {
        execute(); s_shift = false; s_alpha = false;
        refresh(); return;
    }

    if (k == KEY_BACKSPACE) {
        if (s_inlen > 0) s_input[--s_inlen] = 0;
        s_dirty = true; refresh(); return;
    }

    if (k == KEY_UP) {
        if (s_hist_cnt > 0) {
            s_hist_pos = (s_hist_pos + 1) % s_hist_cnt;
            int idx = ((s_hist_head - 1 - s_hist_pos) + SHELL_HISTORY) % SHELL_HISTORY;
            strncpy(s_input, s_hist[idx], SHELL_LINE_LEN);
            s_inlen = (int)strlen(s_input);
            s_dirty = true;
        }
        refresh(); return;
    }

    /* Accept UART input too */
    int uart_c = hal_uart_getc();
    if (uart_c > 0) {
        if (uart_c == '\r' || uart_c == '\n') { execute(); refresh(); return; }
        if (uart_c == 127 && s_inlen > 0) { s_input[--s_inlen] = 0; s_dirty = true; refresh(); return; }
        if (s_inlen < SHELL_LINE_LEN) { s_input[s_inlen++] = (char)uart_c; s_dirty = true; }
    }

    /* Printable key */
    char c = key_to_char(k, s_shift, s_alpha);
    if (c && s_inlen < SHELL_LINE_LEN) {
        s_input[s_inlen++] = c;
        s_dirty = true;
    }

    refresh();
}

void shell_init(void) {
    memset(s_lines, 0, sizeof(s_lines));
    memset(s_input, 0, sizeof(s_input));
    s_nlines = 0; s_inlen = 0;
    shell_redraw();
    shell_puts("NumWorks OS v0.1  Ready.\n");
    shell_puts("Type 'help' for commands.\n");
}
