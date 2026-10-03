/* ================================================================
 * NumWorks OS — Minimal Shell
 * File: shell/shell.c
 *
 * Renders on the LCD display.
 * Input: keypad (alpha+shift modes for text entry)
 * Also accepts characters from UART for debug.
 *
 * Screen layout (320×240, 7×14 font):
 *   44 chars wide
 *   Title bar
 *   13 rows of scrolling output (bottom row = line being printed)
 *   Input line "> " along the bottom edge
 *
 * Code size target: < 5 KB
 * ================================================================ */
#include "shell.h"
#include "commands.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../hal/uart.h"
#include "../hal/font.h"
#include "../ui/line_input.h"
#include "../ui/theme.h"
#include "../include/config.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define CHAR_W  7                                    /* font_small */
#define LINE_H  14
#define COLS    ((LCD_WIDTH - 8) / CHAR_W)           /* 44 */
#define TOP     (UI_TITLE_H + 4)
#define INPUT_H 24
#define INPUT_Y (LCD_HEIGHT - INPUT_H)
#define ROWS    ((INPUT_Y - TOP) / LINE_H)           /* 13 */
/* Input characters that fit after the "> " prompt and before the cursor */
#define INPUT_VISIBLE (COLS - 3)

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
    ui_title_bar_mods("Shell", s_shift, s_alpha);
}

static void draw_output(void) {
    /* Show the last ROWS lines, ending with the line being printed */
    int first = s_nlines - (ROWS - 1);
    for (int r = 0; r < ROWS; r++) {
        int li = first + r;
        int16_t y = (int16_t)(TOP + r * LINE_H);
        display_fill_rect(0, y, LCD_WIDTH, LINE_H, WHITE);
        if (li >= 0) display_text(4, y, s_lines[li % ROWS], &font_small, T_TEXT, WHITE);
    }
}

/* The line along the bottom: a prompt, the text's tail, the cursor */
static void draw_prompt(char prompt, const char *text) {
    display_fill_rect(0, INPUT_Y, LCD_WIDTH, INPUT_H, WHITE);
    display_hline(0, INPUT_Y, LCD_WIDTH, T_GRAY_MIDDLE);
    char line[SHELL_LINE_LEN + 4];
    size_t n = strlen(text);
    snprintf(line, sizeof(line), "%c %s", prompt, n > INPUT_VISIBLE ? text + n - INPUT_VISIBLE : text);
    display_text(4, INPUT_Y + 6, line, &font_small, prompt == '?' ? T_BLUE : T_TEXT, WHITE);
    display_fill_rect((int16_t)(4 + (int)strlen(line) * CHAR_W), INPUT_Y + 5, 1, 16, T_TEXT);
}

static void draw_input(void) {
    draw_header();
    draw_prompt('>', s_input);
}

/* For Python's input(): the line being typed, after a "?" */
static void draw_read_line(const char *text, bool shift, bool alpha) {
    ui_title_bar_mods("Shell", shift, alpha);
    draw_output();
    draw_prompt('?', text);
}

bool shell_read_line(char *buf, int max) {
    return line_input(buf, max, draw_read_line);
}

/* Draw and push to the LCD now: a Python script runs inside one key
 * handler, so the kernel doesn't get to update the screen meanwhile */
void shell_show(void) {
    if (kernel_get_app() != APP_SHELL) return;
    draw_output();
    draw_input();
    display_flush();
    s_dirty = false;
}

void shell_redraw(void) {
    ui_body(WHITE);
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
    if (key_is_exe(k)) {
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

    /* Printable key */
    char c = key_to_char(k, s_shift, s_alpha);
    if (c && s_inlen < SHELL_LINE_LEN) {
        s_input[s_inlen++] = c;
        s_dirty = true;
    }

    refresh();
}

/* Called by the kernel every loop while the shell is shown: serial input
 * (typed in a terminal on the debug UART) works without key presses. */
void shell_tick(void) {
    int c;
    while ((c = hal_uart_getc()) > 0) {
        if (c == '\r' || c == '\n') {
            execute();
        } else if (c == 127 || c == '\b') {
            if (s_inlen > 0) s_input[--s_inlen] = 0;
        } else if (c >= ' ' && c < 127 && s_inlen < SHELL_LINE_LEN) {
            s_input[s_inlen++] = (char)c;
        }
        s_dirty = true;
        if (kernel_get_app() != APP_SHELL) return;   /* a command switched app */
    }
    refresh();
}

void shell_init(void) {
    memset(s_lines, 0, sizeof(s_lines));
    memset(s_input, 0, sizeof(s_input));
    s_nlines = 0; s_inlen = 0;
    shell_redraw();
    shell_puts("NumWorks OS v" NWOS_VERSION "  Ready.\n");
    shell_puts("Type 'help' for commands.\n");
}
