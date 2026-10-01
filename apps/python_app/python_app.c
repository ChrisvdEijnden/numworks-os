/* ================================================================
 * NumWorks OS — Python REPL App
 * File: apps/python_app/python_app.c
 *
 * A MicroPython prompt on the calculator display:
 *  - EXE runs the line. A line that opens a block (def, for, if, ...)
 *    or leaves a bracket open continues with "... " and indents the
 *    next line; an empty line ends the block.
 *  - Output appears while the code runs; input() reads a line from
 *    the keypad.
 *  - Files on the calculator can be imported (`import mymodule` loads
 *    mymodule.py). Leaving the app makes the next import read the
 *    files again, so edits in the editor take effect.
 *  - UP recalls the previous line.
 * ================================================================ */
#include "python_app.h"
#include "../../ui/lang.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../ui/line_input.h"
#include "../../micropython-port/mp_port.h"
#include "../../include/config.h"
#include <string.h>
#include <stdio.h>

#define C_BG   RGB(10,10,20)
#define C_HDR  RGB(30,150,30)
#define C_INPT RGB(20,20,35)
#define C_FOOT RGB(20,60,20)
#define HEADER_H 24
#define FOOTER_H 14
#define INPUT_H  14
#define CHAR_W   7
#define LINE_H   12
#define COLS     ((LCD_WIDTH - 4) / CHAR_W)                                /* 45 */
#define OROWS    ((LCD_HEIGHT - HEADER_H - INPUT_H - FOOTER_H) / LINE_H)  /* 15 */
#define INPUT_Y  (LCD_HEIGHT - FOOTER_H - INPUT_H)
#define LINE_MAX 128          /* one input line */
#define BLOCK_MAX 1024        /* a whole multi-line entry */

/* Output ring: line n lives in s_out[n % OROWS] */
static char s_out[OROWS][COLS+1];
static int  s_nout = 0;

static char s_line[LINE_MAX];      /* the line being typed */
static int  s_len = 0;
static char s_last[LINE_MAX];      /* previous line, for UP */
static char s_block[BLOCK_MAX];    /* lines entered so far of an open block */
static int  s_blen = 0;
static bool s_cont = false;        /* continuing a block: "... " prompt */
static bool s_shift = false;
static bool s_alpha = false;
static bool s_left = false;        /* the user left the app since the last run */

/* ── Output ───────────────────────────────────────────────────── */
static void out_write(const char *s, size_t n) {
    while (n) {
        int row = s_nout % OROWS;
        int col = (int)strlen(s_out[row]);
        while (n && *s != '\n' && col < COLS) {
            s_out[row][col++] = *s++;
            s_out[row][col] = 0;
            n--;
        }
        if ((n && *s == '\n') || col >= COLS) {
            s_nout++;
            /* wrap, keeping s_nout % OROWS so the ring stays in order */
            if (s_nout > 1000000) s_nout = OROWS + s_nout % OROWS;
            memset(s_out[s_nout % OROWS], 0, COLS+1);
            if (n && *s == '\n') { s++; n--; }
        }
    }
}

static void out_print(const char *s) { out_write(s, strlen(s)); }

/* The last OROWS lines, ending with the line being printed */
static void draw_output(void) {
    int first = s_nout - (OROWS - 1);
    for (int r = 0; r < OROWS; r++) {
        int li = first + r;
        int y  = HEADER_H + r * LINE_H;
        display_fill_rect(0, y, LCD_WIDTH, LINE_H, C_BG);
        if (li >= 0) display_str(2, y + 2, s_out[li % OROWS], RGB(180,255,180), C_BG);
    }
}

/* ── Input line ───────────────────────────────────────────────── */
static void draw_line(const char *prompt, const char *text, bool shift, bool alpha) {
    display_fill_rect(0, INPUT_Y, LCD_WIDTH, INPUT_H, C_INPT);
    const char *mode = alpha ? (shift ? "ABC" : "abc") : (shift ? "SHF" : "");
    int pl = (int)strlen(prompt);
    int room = COLS - pl - 1 - 4;         /* prompt, cursor, mode */
    int n = (int)strlen(text);
    char vis[COLS+1];
    snprintf(vis, sizeof(vis), "%s%s_", prompt, n > room ? text + n - room : text);
    display_str(2, INPUT_Y + 3, vis, WHITE, C_INPT);
    display_str(LCD_WIDTH - 2 - 3 * CHAR_W, INPUT_Y + 3, mode, RGB(150,200,150), C_INPT);
}

static void draw_footer(void) {
    display_fill_rect(0, LCD_HEIGHT - FOOTER_H, LCD_WIDTH, FOOTER_H, C_FOOT);
    display_str(4, LCD_HEIGHT - FOOTER_H + 3,
                s_cont ? TR("Lege regel + EXE: blok uitvoeren", "Empty line + EXE: run the block")
                       : TR("EXE:Uitvoeren ALPHA:Letters HOME:Terug", "EXE:Run  ALPHA:Letters  HOME:Back"),
                YELLOW, C_FOOT);
}

static void draw_input(void) {
    draw_line(s_cont ? "... " : ">>> ", s_line, s_shift, s_alpha);
}

/* ── Console for mp_port: live output and input() ─────────────── */
static void console_show(void) {
    draw_output();
    display_flush();
}

static void draw_read_line(const char *text, bool shift, bool alpha) {
    draw_output();
    draw_line("? ", text, shift, alpha);
}

static bool console_read_line(char *buf, int max) {
    return line_input(buf, max, draw_read_line);
}

static const mp_console_t s_console = { out_write, console_read_line, console_show };

/* ── Entering code ────────────────────────────────────────────── */
static bool is_blank(const char *s) {
    while (*s == ' ') s++;
    return *s == 0;
}

/* Indentation for the next line of a block: that of the last line,
 * one level deeper after a line ending in ':' */
static int next_indent(void) {
    int start = s_blen;
    while (start > 0 && s_block[start - 1] != '\n') start--;
    int ind = 0;
    while (start + ind < s_blen && s_block[start + ind] == ' ') ind++;
    int end = s_blen;
    while (end > start && s_block[end - 1] == ' ') end--;
    if (end > start && s_block[end - 1] == ':') ind += 4;
    return ind < LINE_MAX - 1 ? ind : LINE_MAX - 1;
}

static void set_line(const char *text) {
    strncpy(s_line, text, LINE_MAX - 1);
    s_line[LINE_MAX - 1] = 0;
    s_len = (int)strlen(s_line);
}

static void run_block(void) {
    if (s_left) { mp_forget_imports(); s_left = false; }
    display_fill_rect(0, INPUT_Y, LCD_WIDTH, INPUT_H, C_INPT);
    display_str(2, INPUT_Y + 3, TR("Bezig...  BACK stopt", "Running...  BACK stops"), RGB(150,200,150), C_INPT);
    mp_set_console(&s_console);
    mp_exec_repl(s_block);
    mp_set_console(NULL);
    mp_pause_after_graphics();
    s_blen = 0; s_block[0] = 0;
    s_cont = false;
}

static void enter_line(void) {
    bool blank = is_blank(s_line);
    if (!s_cont && blank) return;
    out_print(s_cont ? "... " : ">>> ");
    out_print(s_line);
    out_print("\n");
    if (!blank) strcpy(s_last, s_line);

    /* Lines are joined with '\n'; an empty line leaves the entry ending
     * in '\n', which is what closes a block */
    int need = (s_cont ? 1 : 0) + (blank ? 0 : s_len);
    if (s_blen + need >= BLOCK_MAX) {
        out_print(TR("Invoer te lang\n", "Input too long\n"));
        s_blen = 0; s_block[0] = 0; s_cont = false;
        set_line("");
        return;
    }
    if (s_cont) s_block[s_blen++] = '\n';
    if (!blank) { memcpy(s_block + s_blen, s_line, (size_t)s_len); s_blen += s_len; }
    s_block[s_blen] = 0;

    if (mp_repl_incomplete(s_block)) {
        s_cont = true;
        int ind = next_indent();
        memset(s_line, ' ', (size_t)ind);
        s_line[ind] = 0;
        s_len = ind;
    } else {
        set_line("");
        run_block();
    }
}

/* ── App interface ────────────────────────────────────────────── */
void python_app_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(6, 8, "Python", WHITE, C_HDR);
    display_str(LCD_WIDTH - 84, 8, TR("HOME:Terug", "HOME:Back"), RGB(180,255,180), C_HDR);
    draw_output();
    draw_input();
    draw_footer();
}

void python_app_init(void) {
    s_nout = 0;
    memset(s_out, 0, sizeof(s_out));
    s_len = 0; s_line[0] = 0; s_last[0] = 0;
    s_blen = 0; s_block[0] = 0; s_cont = false;
    out_print(TR("MicroPython\nBACK onderbreekt een lopend script\n", "MicroPython\nBACK interrupts a running script\n"));
}

void python_app_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) {
        s_left = true;
        mp_close_files();               /* save what the session left open */
        kernel_set_app(APP_HOME);
        return;
    }
    if (k == KEY_SHIFT) { s_shift = !s_shift; draw_input(); return; }
    if (k == KEY_ALPHA) { s_alpha = !s_alpha; draw_input(); return; }
    if (key_is_exe(k)) {
        s_shift = false;
        enter_line();
        python_app_redraw();
        return;
    }
    if (k == KEY_UP) { set_line(s_last); draw_input(); return; }
    if (k == KEY_BACKSPACE) {
        /* In leading indentation, remove one level at a time */
        if (s_len > 0 && is_blank(s_line)) s_len = ((s_len - 1) / 4) * 4;
        else if (s_len > 0) s_len--;
        s_line[s_len] = 0;
        draw_input();
        return;
    }
    char c = key_to_char(k, s_shift, s_alpha);
    if (c && s_len < LINE_MAX - 1) {
        s_line[s_len++] = c;
        s_line[s_len] = 0;
        s_shift = false;
        draw_input();
    }
}
