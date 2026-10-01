/* ================================================================
 * NumWorks OS — Blocking line input
 * File: ui/line_input.c
 * ================================================================ */
#include "line_input.h"
#include "../hal/hal.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../include/string.h"

bool line_input(char *buf, int max,
                void (*draw)(const char *text, bool shift, bool alpha)) {
    int len = (int)strlen(buf);
    bool shift = false, alpha = false, changed = true;
    for (;;) {
        if (changed) {
            draw(buf, shift, alpha);
            display_flush();
            changed = false;
        }
        hal_delay_ms(5);

        key_event_t ev;
        while (keyboard_poll(&ev)) {
            if (ev.action != 0) continue;
            key_code_t k = (key_code_t)ev.key;
            if (k == KEY_BACK || k == KEY_HOME) return false;
            if (key_is_exe(k)) return true;
            if (k == KEY_SHIFT)          shift = !shift;
            else if (k == KEY_ALPHA)     alpha = !alpha;
            else if (k == KEY_BACKSPACE) { if (len > 0) buf[--len] = 0; }
            else {
                char c = key_to_char(k, shift, alpha);
                if (!c || len >= max - 1) continue;
                buf[len++] = c;
                buf[len] = 0;
                shift = false;
            }
            changed = true;
        }

        int c;
        while ((c = hal_uart_getc()) > 0) {
            if (c == '\r' || c == '\n') return true;
            if (c == 3) return false;                       /* Ctrl-C */
            if ((c == 127 || c == '\b') && len > 0) buf[--len] = 0;
            else if (c >= ' ' && c < 127 && len < max - 1) { buf[len++] = (char)c; buf[len] = 0; }
            changed = true;
        }
    }
}
