#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/kernel.h"
#include "../../hal/keyboard.h"
void functions_init(void); void functions_handle_event(const kernel_event_t*); const char *ft_fn(int); int ft_nfn(void);
#include "../../apps/functions/functions.h"
#include "../../apps/common/analysis.h"
#include "../../ui/lang.h"
#include <math.h>
void text_editor_init(void); void text_editor_handle_event(const kernel_event_t*); const char *et_name(void); bool et_modified(void);
void display_pixel(int16_t x, int16_t y, uint16_t c) {(void)x;(void)y;(void)c;}
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {(void)x;(void)y;(void)w;(void)h;(void)c;}
void display_hline(int16_t x, int16_t y, int16_t w, uint16_t c) {(void)x;(void)y;(void)w;(void)c;}
void display_vline(int16_t x, int16_t y, int16_t h, uint16_t c) {(void)x;(void)y;(void)h;(void)c;}
void display_fill(uint16_t c) {(void)c;}
void display_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {(void)x;(void)y;(void)w;(void)h;(void)c;}
void display_str_len(int16_t x, int16_t y, const char *s, int len, uint16_t a, uint16_t b) {(void)x;(void)y;(void)s;(void)len;(void)a;(void)b;}
static char footer[128];
void display_str(int16_t x, int16_t y, const char *s, uint16_t a, uint16_t b) {(void)x;(void)a;(void)b; if (y >= 226) snprintf(footer, sizeof footer, "%s", s);}
void kernel_set_app(app_state_t a) {(void)a;}
char key_to_char(key_code_t k, bool shift, bool alpha);   /* real one from keyboard.c (linked) */
/* in-memory flashfs */
static char files[8][24]; static char data[8][256]; static int nfiles = 0;
bool flashfs_exists(const char *p) { for (int i = 0; i < nfiles; i++) if (!strcmp(files[i], p)) return true; return false; }
int flashfs_write(const char *p, const void *d, uint32_t n) { int i; for (i = 0; i < nfiles && strcmp(files[i], p); i++); if (i == nfiles) strcpy(files[nfiles++], p); memcpy(data[i], d, n); data[i][n] = 0; return (int)n; }
int flashfs_open_read(const char *p, uint32_t *o, uint32_t *s) {(void)p;(void)o;(void)s; return -1;}
int flashfs_read(uint32_t o, void *b, uint32_t l) {(void)o;(void)b;(void)l; return -1;}
static int fails = 0;
static void check(int c, const char *w) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }
static void fk(key_code_t k) { kernel_event_t e = { .key = k, .action = 0 }; functions_handle_event(&e); }
static void ek(key_code_t k) { kernel_event_t e = { .key = k, .action = 0 }; text_editor_handle_event(&e); }
#define FN(name, expr) static double name(void *c, double v) { (void)c; return expr; }
FN(recip, 1.0 / v) FN(tanf_, tan(v)) FN(cubic, v * v * v - 2 * v + 1) FN(lnf, log(v)) FN(flat, 3.0)
int main(void) {
    puts("functions:");
    functions_init();
    key_code_t digits[4] = {KEY_1, KEY_2, KEY_3, KEY_4};
    for (int i = 0; i < 4; i++) { fk(KEY_XNT); fk(KEY_PLUS); fk(digits[i]); fk(KEY_OK); }
    check(ft_nfn() == 4 && !strcmp(ft_fn(3), "x+4"), "four functions stored (was the hard limit before)");
    fk(KEY_UP); fk(KEY_UP); fk(KEY_BACKSPACE); fk(KEY_9); fk(KEY_OK);   /* f2: x+2 -> x+9 */
    check(!strcmp(ft_fn(1), "x+9"), "f2 selected with UP and edited to x+9");
    fk(KEY_UP); fk(KEY_UP); fk(KEY_UP); fk(KEY_UP);                       /* top: f1 */
    fk(KEY_BACKSPACE); fk(KEY_BACKSPACE); fk(KEY_BACKSPACE); fk(KEY_OK); /* empty + OK */
    check(ft_nfn() == 3 && !strcmp(ft_fn(0), "x+9") && !strcmp(ft_fn(2), "x+4"), "emptied f1 deleted, rest moved up");
    fk(KEY_LPAREN); fk(KEY_XNT); fk(KEY_OK);
    check(ft_nfn() == 3, "unbalanced '(x' rejected, nothing stored");
    puts("editor:");
    text_editor_init();
    ek(KEY_ALPHA); ek(KEY_COS); ek(KEY_EXP); ek(KEY_7);                 /* "ham" */
    ek(KEY_SHIFT); ek(KEY_OK);                                         /* save -> asks name */
    check(strstr(footer, "Naam:") != NULL, "first save of a new document asks for a name");
    ek(KEY_8); ek(KEY_EXP); ek(KEY_7); ek(KEY_COMMA); ek(KEY_OK);        /* name "name" */
    check(!strcmp(et_name(), "name") && flashfs_exists("name") && !et_modified(), "saved as 'name', not noname.txt");
    text_editor_init(); ek(KEY_ALPHA); ek(KEY_SIN); ek(KEY_SHIFT); ek(KEY_OK);
    ek(KEY_8); ek(KEY_EXP); ek(KEY_7); ek(KEY_COMMA); ek(KEY_OK);        /* same name again */
    check(strstr(footer, "bestaat al") != NULL && et_modified(), "existing name refused, document not saved over it");
    puts("graph: trace and analysis:");
    functions_init();
    fk(KEY_XNT); fk(KEY_SQUARE); fk(KEY_MINUS); fk(KEY_2); fk(KEY_OK);      /* f1 = x^2-2 */
    fk(KEY_XNT); fk(KEY_OK);                                                 /* f2 = x */
    check(ft_nfn() == 2 && !strcmp(ft_fn(0), "x^2-2"), "f1 = x^2-2, f2 = x");
    fk(KEY_TOOLBOX);                                                         /* graph */
    fk(KEY_OK);
    check(functions_tracing() && fabs(functions_cursor_x()) < 1e-9, "OK starts tracing at the centre (x=0)");
    fk(KEY_RIGHT);
    check(functions_cursor_x() > 0.2 && functions_cursor_x() < 0.3 && strstr(footer, "f1  x=0.25  y=-1.9375"), "RIGHT moves the cursor 4 px; footer shows x and f(x)");
    fk(KEY_TOOLBOX); fk(KEY_OK);                                             /* analysis: zero */
    check(fabs(functions_cursor_x() - sqrt(2.0)) < 1e-9 && strstr(footer, "Nulpunt: x=1.41421"), "zero right of the cursor: x = sqrt(2), cursor moved there");
    fk(KEY_TOOLBOX); fk(KEY_DOWN); fk(KEY_OK);                               /* minimum right of sqrt(2) */
    check(strstr(footer, "geen gevonden") != NULL, "no minimum to the right of sqrt(2): reported");
    fk(KEY_BACK);                                                            /* stop tracing */
    check(!functions_tracing(), "BACK stops tracing");
    fk(KEY_TOOLBOX); fk(KEY_DOWN); fk(KEY_OK);
    printf("       footer: %s  x=%g\n", footer, functions_cursor_x());
    check(fabs(functions_cursor_x()) < 1e-6 && strstr(footer, "Minimum: x=0 y=-2"), "minimum from the left edge: (0, -2)");
    fk(KEY_BACK);
    fk(KEY_TOOLBOX); fk(KEY_DOWN); fk(KEY_DOWN); fk(KEY_DOWN); fk(KEY_OK);   /* intersection */
    check(fabs(functions_cursor_x() + 1.0) < 1e-9 && strstr(footer, "f2") != NULL, "first intersection with f2: x = -1");
    fk(KEY_TOOLBOX); fk(KEY_DOWN); fk(KEY_DOWN); fk(KEY_DOWN); fk(KEY_OK);
    check(fabs(functions_cursor_x() - 2.0) < 1e-9, "the next one: x = 2");
    fk(KEY_DOWN);                                                            /* trace f2 */
    check(strstr(footer, "f2  x=2  y=2") != NULL, "DOWN moves the cursor to f2");
    for (int i = 0; i < 60; i++) fk(KEY_RIGHT);
    check(functions_cursor_x() > 13 && strstr(footer, "f2") != NULL, "tracing past the edge pans the window");
    lang_set(LANG_EN);
    fk(KEY_TOOLBOX); fk(KEY_DOWN); fk(KEY_DOWN); fk(KEY_OK);                 /* maximum: none */
    check(strstr(footer, "Maximum: none found") != NULL, "in English too");
    lang_set(LANG_NL);
    puts("analysis routines:");
    double x;
    check(!an_next_zero(recip, NULL, -10, 10, &x), "1/x: the sign change at 0 is a pole, not a zero");
    check(an_next_zero(tanf_, NULL, 1, 10, &x) && fabs(x - M_PI) < 1e-9, "tan: next zero after 1 is pi (poles at pi/2 skipped)");
    check(an_next_zero(cubic, NULL, -5, 5, &x) && fabs(x * x * x - 2 * x + 1) < 1e-9 && x < -1.5, "cubic: first root -1.618");
    check(an_next_extremum(cubic, NULL, -5, 5, true, &x) && fabs(x + sqrt(2.0 / 3)) < 1e-7, "cubic: local maximum at -sqrt(2/3)");
    check(an_next_extremum(cubic, NULL, -5, 5, false, &x) && fabs(x - sqrt(2.0 / 3)) < 1e-7, "cubic: local minimum at +sqrt(2/3)");
    check(an_next_zero(lnf, NULL, -3, 3, &x) && fabs(x - 1) < 1e-9, "ln: undefined left of 0, zero at 1");
    check(!an_next_zero(flat, NULL, -3, 3, &x) && !an_next_extremum(flat, NULL, -3, 3, true, &x), "a constant: no zero, no extremum");
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
