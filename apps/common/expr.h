/* ================================================================
 * NumWorks OS — Expression evaluator (shared by the math apps)
 * File: apps/common/expr.h
 *
 * Evaluates expressions such as "2x^2 - 3sin(x)/ln(10)" natively,
 * without MicroPython. Supports + - * / ^ (or **), implicit
 * multiplication ("2x", "3(x+1)"), the variable x, the constants pi
 * and e, the previous result ans, scientific notation (2E3) and the
 * functions sin cos tan asin
 * acos atan sinh cosh tanh sqrt cbrt ln log (base 10) log10 exp abs.
 * Angles are in radians.
 * ================================================================ */
#pragma once
#include <stdbool.h>
#include "../../hal/keyboard.h"

typedef enum {
    EXPR_OK = 0,
    EXPR_ERR_EMPTY,     /* nothing to evaluate        */
    EXPR_ERR_SYNTAX,    /* unexpected or missing token */
    EXPR_ERR_PAREN,     /* unbalanced parentheses      */
    EXPR_ERR_NAME,      /* unknown function / constant */
    EXPR_ERR_DEPTH,     /* nested too deeply           */
} expr_status_t;

/* Evaluate `src` with the variable x set to `x`. On EXPR_OK, *out holds
 * the value, which can be NaN or infinite outside a function's domain
 * (sqrt(-1), 1/0). */
expr_status_t expr_eval(const char *src, double x, double *out);

/* Value of `ans` in later expressions (the calculator's last result) */
void expr_set_ans(double v);

/* Short description of an error status, in the interface language */
const char *expr_error(expr_status_t st);

/* Format a result for display: up to 10 significant digits */
void expr_format(double v, char *buf, int len);

/* Text a key types into an expression, or NULL if none. SHIFT gives
 * the inverse function (sin -> asin, ln -> exp, ...). */
const char *expr_key_text(key_code_t k, bool shift);
