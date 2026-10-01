
/* ================================================================
 * NumWorks OS — Python REPL App
 * File: apps/python_app/python_app.c
 *
 * A MicroPython interactive REPL on the calculator display.
 * Multi-line input supported (SHIFT+EXE for newline, EXE executes).
 * ================================================================ */
#include "python_app.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../micropython-port/mp_port.h"
#include "../../include/config.h"
#include "../../include/string.h"
#include "../../include/stdio.h"

#define C_BG   RGB(10,10,20)
#define C_HDR  RGB(30,150,30)
#define C_INPT RGB(20,20,35)
#define HEADER_H 24
#define FOOTER_H 16
#define OUT_H    (LCD_HEIGHT - HEADER_H - FOOTER_H - 36)

#define COLS   40
#define OROWS  12

static char s_output[OROWS][COLS+1];
static int  s_nout = 0;
static char s_input[256];
static int  s_ilen = 0;
static bool s_shift = false;
static bool s_alpha = false;

static void py_print(const char *s) {
    while (*s) {
        int row = s_nout % OROWS;
        int col = (int)strlen(s_output[row]);
        while (*s && *s != '\n' && col < COLS) {
            s_output[row][col++] = *s++;
            s_output[row][col] = 0;
        }
        if (*s == '\n' || col >= COLS) {
            s_nout++;
            /* wrap, keeping s_nout % OROWS so the ring stays in order */
            if (s_nout > 1000000) s_nout = OROWS + s_nout % OROWS;
            int nr = s_nout % OROWS;
            memset(s_output[nr], 0, COLS+1);
            if (*s == '\n') s++;
        }
    }
}

/* s_output is a ring: line n lives in s_output[n % OROWS]. Show the
 * last OROWS lines, ending with the line being printed. */
static void draw_output(void) {
    int first = s_nout - (OROWS - 1);
    for (int r = 0; r < OROWS; r++) {
        int li = first + r;
        int y  = HEADER_H + r * 14;
        if (li >= 0) {
            char padded[COLS+1];
            snprintf(padded, sizeof(padded), "%-*s", COLS, s_output[li % OROWS]);
            display_str(0, y, padded, RGB(180,255,180), C_BG);
        } else {
            display_fill_rect(0, y, LCD_WIDTH, 14, C_BG);
        }
    }
}

static void draw_input(void) {
    int iy = LCD_HEIGHT - FOOTER_H - 34;
    display_fill_rect(0, iy, LCD_WIDTH, 34, C_INPT);
    display_str(2, iy+2, ">>> ", YELLOW, C_INPT);
    display_str(28, iy+2, s_input, WHITE, C_INPT);
    char mode[16];
    snprintf(mode, sizeof(mode), "%s%s", s_shift?"SHF ":"", s_alpha?"ABC":"");
    display_str(2, iy+16, mode, RGB(150,200,150), C_INPT);
}

static void execute(void) {
    if (s_ilen == 0) return;
    py_print(">>> "); py_print(s_input); py_print("\n");
    char out[256];
    int r = mp_exec_repl_capture(s_input, out, sizeof(out));
    if (r >= 0 && out[0]) { py_print(out); }
    else if (r < 0) { py_print("Uitvoeringsfout\n"); }
    s_input[0] = 0; s_ilen = 0;
}

void python_app_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    display_str(6, 6, "Python REPL", WHITE, C_HDR);
    display_str(LCD_WIDTH-66, 6, "HOME:Terug", RGB(180,255,180), C_HDR);
    draw_output();
    draw_input();
    display_fill_rect(0, LCD_HEIGHT-FOOTER_H, LCD_WIDTH, FOOTER_H, RGB(20,60,20));
    display_str(4, LCD_HEIGHT-FOOTER_H+4,
                "EXE:Uitvoer  SHIFT:Hoofdl  ALPHA:Letters",
                YELLOW, RGB(20,60,20));
}

void python_app_init(void) {
    s_nout = 0; s_ilen = 0;
    memset(s_output, 0, sizeof(s_output));
    py_print("MicroPython REPL\nBACK onderbreekt een lopend script\n");
}

void python_app_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k == KEY_HOME || k == KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k == KEY_SHIFT) { s_shift = !s_shift; draw_input(); return; }
    if (k == KEY_ALPHA) { s_alpha = !s_alpha; draw_input(); return; }
    if (key_is_exe(k)) { execute(); python_app_redraw(); return; }
    if (k == KEY_BACKSPACE) {
        if (s_ilen > 0) s_input[--s_ilen] = 0;
        draw_input(); return;
    }
    char c = key_to_char(k, s_shift, s_alpha);
    if (c && s_ilen < 255) {
        s_input[s_ilen++] = c; s_input[s_ilen] = 0;
        draw_input();
        s_shift = false;
    }
}
