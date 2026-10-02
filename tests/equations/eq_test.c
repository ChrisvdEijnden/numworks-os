#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/kernel.h"
#include "../../hal/keyboard.h"
/* display stubs: remember result lines (drawn in white below the fields) */
static char lines[16][80]; static int nlines;
void display_str(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg) { (void)x;(void)bg; if (fg == 0xFFFF && y >= 148 && nlines < 16) snprintf(lines[nlines++], 80, "%s", s); }
void display_fill(uint16_t c) { (void)c; }
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { (void)x;(void)w;(void)h;(void)c; if (y >= 148) nlines = 0; }
void kernel_set_app(app_state_t a) { (void)a; }
char key_to_char(key_code_t k, bool s, bool a) { (void)k;(void)s;(void)a; return 0; }
void equations_init(void); void equations_handle_event(const kernel_event_t *ev);
char *eqt_single(void); char *eqt_input(void);
#define s_single eqt_single()
#define s_input eqt_input()
static void key(key_code_t k) { kernel_event_t ev = { .key = k, .action = 0 }; equations_handle_event(&ev); }
static int fails = 0;
static void expect(const char *what, const char *want) {
    int found = 0; for (int i = 0; i < nlines; i++) if (strstr(lines[i], want)) found = 1;
    printf("  %s %-34s -> %s\n", found ? "ok  " : "FAIL", what, nlines ? lines[0] : "(nothing)");
    if (!found) fails++;
}
int main(void) {
    equations_init();
    /* quadratic x^2 - 3x + 2: edit b to -3 and c to 2 by keys */
    key(KEY_DOWN); key(KEY_ALPHA); key(KEY_BACKSPACE); key(KEY_MINUS); key(KEY_3); key(KEY_OK);
    key(KEY_DOWN); key(KEY_ALPHA); key(KEY_BACKSPACE); key(KEY_2); key(KEY_OK);
    key(KEY_OK); expect("x^2-3x+2 = 0", "x1 = 2");
    /* field accepts an expression: a = 1/2 -> 0.5x^2-3x+2 */
    key(KEY_UP); key(KEY_UP); key(KEY_ALPHA); key(KEY_BACKSPACE); key(KEY_1); key(KEY_DIV); key(KEY_2); key(KEY_OK);
    key(KEY_OK); expect("0.5x^2-3x+2 (a typed as 1/2)", "x1 = 5.236067977");
    /* single equation: switch mode twice, type x^2-2 */
    key(KEY_RIGHT); key(KEY_RIGHT);
    key(KEY_ALPHA); key(KEY_XNT); key(KEY_POW); key(KEY_2); key(KEY_MINUS); key(KEY_2); key(KEY_OK);
    key(KEY_OK); expect("x^2-2 = 0", "x = 1.414213562");
    struct { const char *f, *want; } cases[] = {
        {"ln(x)-1", "x = 2.718281828"}, {"x^3-x-2", "x = 1.521379707"},
        {"cos(x)-x", "x = 0.7390851332"}, {"exp(x)+1", "Geen oplossing"},
        {"sin(x", "Fout: haakjes"}, {"2x+", "Fout: syntaxfout"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        strcpy(s_single, cases[i].f); key(KEY_OK);
        char what[64]; snprintf(what, sizeof what, "%s = 0", cases[i].f); expect(what, cases[i].want);
    }
    /* backspace then type: the character must not vanish */
    key(KEY_ALPHA); key(KEY_BACKSPACE); key(KEY_BACKSPACE); key(KEY_7);
    printf("  %s backspace then type -> input '%s'\n", strcmp(s_input, "27") == 0 ? "ok  " : "FAIL", s_input);
    if (strcmp(s_input, "27")) fails++;
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
