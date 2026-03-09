/* ================================================================
 * NumWorks OS — Minimal Shell
 * File: shell/shell.c
 *
 * Renders on the LCD display.
 * Input: keypad (alpha+shift modes for text entry)
 * Also accepts characters from UART for debug.
 *
 * Screen layout (320×240, 6×8 font, 1px spacing):
 *   40 chars wide, 26 rows
 *   Top bar (row 0): "NWsOS > "  (header)
 *   Rows 1–24: scrolling output
 *   Row 25: input line  "> _"
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
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define COLS    40
#define ROWS    24
#define CHAR_W  (FONT_W + 1)
#define CHAR_H  (FONT_H + 2)
#define BAR_H   18
#define INPUT_Y (BAR_H + ROWS * CHAR_H + 2)

/* Scrollback buffer */
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
    int start = s_nlines > ROWS ? s_nlines - ROWS : 0;
    for (int r = 0; r < ROWS; r++) {
        int li = start + r;
        int y  = BAR_H + r * CHAR_H;
        if (li < s_nlines) {
            /* Pad to clear old content */
            char padded[COLS+1];
            snprintf(padded, sizeof(padded), "%-*s", COLS, s_lines[li]);
            display_str(0, y, padded, GREEN, BLACK);
        } else {
            display_fill_rect(0, y, LCD_WIDTH, CHAR_H, BLACK);
        }
    }
}

static void draw_input(void) {
    display_fill_rect(0, INPUT_Y - 2, LCD_WIDTH, CHAR_H + 4, DKGREY);
    char prompt[SHELL_LINE_LEN + 4];
    char mode = s_alpha ? (s_shift ? 'A' : 'a') : (s_shift ? '^' : '>');
    snprintf(prompt, sizeof(prompt), "%c %s_", mode, s_input);
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
            if (s_nlines > 9999) s_nlines = ROWS;  /* wrap */
            int nr = s_nlines % ROWS;
            memset(s_lines[nr], 0, COLS+1);
            if (*s == '\n') s++;
        }
    }
    s_dirty = true;
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

void shell_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;  /* Only handle key presses */

    key_code_t k = (key_code_t)ev->key;

    /* Mode keys */
    if (k == KEY_SHIFT) { s_shift = !s_shift; s_dirty = true; return; }
    if (k == KEY_ALPHA) { s_alpha = !s_alpha; s_dirty = true; return; }

    /* Navigation */
    if (k == KEY_EXE || k == KEY_OK) { execute(); s_shift = false; s_alpha = false; return; }

    if (k == KEY_BACKSPACE) {
        if (s_inlen > 0) s_input[--s_inlen] = 0;
        s_dirty = true; return;
    }

    if (k == KEY_UP) {
        if (s_hist_cnt > 0) {
            s_hist_pos = (s_hist_pos + 1) % s_hist_cnt;
            int idx = ((s_hist_head - 1 - s_hist_pos) + SHELL_HISTORY) % SHELL_HISTORY;
            strncpy(s_input, s_hist[idx], SHELL_LINE_LEN);
            s_inlen = (int)strlen(s_input);
            s_dirty = true;
        }
        return;
    }

    /* Accept UART input too */
    int uart_c = hal_uart_getc();
    if (uart_c > 0) {
        if (uart_c == '\r' || uart_c == '\n') { execute(); return; }
        if (uart_c == 127 && s_inlen > 0) { s_input[--s_inlen] = 0; s_dirty = true; return; }
        if (s_inlen < SHELL_LINE_LEN) { s_input[s_inlen++] = (char)uart_c; s_dirty = true; }
    }

    /* Printable key */
    char c = key_to_char(k, s_shift, s_alpha);
    if (c && s_inlen < SHELL_LINE_LEN) {
        s_input[s_inlen++] = c;
        s_dirty = true;
    }

    if (s_dirty) {
        draw_output();
        draw_input();
        s_dirty = false;
    }
}

void shell_init(void) {
    memset(s_lines, 0, sizeof(s_lines));
    memset(s_input, 0, sizeof(s_input));
    s_nlines = 0; s_inlen = 0;
    shell_redraw();
    shell_puts("NumWorks OS v0.1  Ready.\n");
    shell_puts("Type 'help' for commands.\n");
}
