#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../../apps/common/expr.h"
static int fails = 0;
static void ok(const char *e, double x, double want) {
    double v = -12345; expr_status_t st = expr_eval(e, x, &v);
    int good = st == EXPR_OK && (fabs(v - want) <= 1e-9 * (1 + fabs(want)) || (isnan(want) && isnan(v)) || (isinf(want) && v == want));
    if (!good) { printf("  FAIL %-22s x=%g -> st=%d v=%.12g (want %.12g)\n", e, x, st, v, want); fails++; }
}
static void err(const char *e, expr_status_t want) {
    double v; expr_status_t st = expr_eval(e, 0, &v);
    if (st != want) { printf("  FAIL %-22s -> st=%d (want %d)\n", e, st, want); fails++; }
}
int main(void) {
    ok("1+2*3", 0, 7);        ok("(1+2)*3", 0, 9);      ok("2^3^2", 0, 512);
    ok("-2^2", 0, -4);        ok("2^-1", 0, 0.5);       ok("2**3", 0, 8);
    ok("10/4", 0, 2.5);       ok("7-2-1", 0, 4);        ok("8/2/2", 0, 2);
    ok("2x", 3, 6);           ok("2x^2", 3, 18);        ok("3(x+1)", 2, 9);
    ok("(x+1)(x-1)", 3, 8);   ok("2pi", 0, 2*M_PI);     ok("2sin(x)", M_PI/2, 2);
    ok("x*sin(x)", 1, sin(1)); ok("sqrt(16)", 0, 4);    ok("cbrt(27)", 0, 3);
    ok("ln(e)", 0, 1);        ok("log(1000)", 0, 3);    ok("log10(100)", 0, 2);
    ok("exp(0)", 0, 1);       ok("abs(-3.5)", 0, 3.5);  ok("asin(1)", 0, M_PI/2);
    ok("2E3", 0, 2000);       ok("1.5e-2", 0, 0.015);   ok("2e", 0, 2*M_E);
    ok(".5+.25", 0, 0.75);    ok(" 1 + 2 ", 0, 3);      ok("--3", 0, 3);
    ok("10^2", 0, 100);       ok("e^(ln(5))", 0, 5);    ok("2 x", 4, 8);
    ok("sqrt(-1)", 0, NAN);   ok("1/0", 0, INFINITY);   ok("ln(0)", 0, -INFINITY);
    err("", EXPR_ERR_EMPTY);  err("   ", EXPR_ERR_EMPTY);
    err("1+", EXPR_ERR_SYNTAX); err("*2", EXPR_ERR_SYNTAX); err("2 $ 3", EXPR_ERR_SYNTAX);
    err("sin(1", EXPR_ERR_PAREN); err("(1+2", EXPR_ERR_PAREN); err("1+2)", EXPR_ERR_PAREN);
    err("sin 1", EXPR_ERR_SYNTAX); err("foo(1)", EXPR_ERR_NAME); err("y+1", EXPR_ERR_NAME);
    char deep[400]; memset(deep, '(', 300); deep[300] = '1'; deep[301] = 0;
    err(deep, EXPR_ERR_DEPTH);
    char ok30[80]; memset(ok30, '(', 30); ok30[30] = '2'; memset(ok30 + 31, ')', 30); ok30[61] = 0;
    ok(ok30, 0, 2);
    char minus[300]; memset(minus, '-', 250); minus[250] = '1'; minus[251] = 0;
    err(minus, EXPR_ERR_DEPTH);
    char buf[32];
    expr_format(-0.0, buf, sizeof buf);    if (strcmp(buf, "0")) { printf("  FAIL format -0 -> %s\n", buf); fails++; }
    expr_format(1.0/3, buf, sizeof buf);   if (strcmp(buf, "0.3333333333")) { printf("  FAIL format 1/3 -> %s\n", buf); fails++; }
    expr_format(NAN, buf, sizeof buf);     if (strcmp(buf, "ongedefinieerd")) { printf("  FAIL format nan -> %s\n", buf); fails++; }
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
