/* ================================================================
 * NumWorks OS — Expression evaluator
 * File: apps/common/expr.c
 *
 * Recursive descent, one function per precedence level:
 *   expr  := term (('+'|'-') term)*
 *   term  := unary (('*'|'/') unary | implicit-mult power)*
 *   unary := ('-'|'+') unary | power
 *   power := primary (('^'|'**') unary)?          right-associative
 *   primary := number | x | pi | e | func '(' expr ')' | '(' expr ')'
 * so -2^2 = -4 and 2^3^2 = 2^9, as on paper.
 * ================================================================ */
#include "expr.h"
#include "../../include/math.h"
#include "../../include/stdio.h"
#include "../../include/stdlib.h"
#include "../../include/string.h"

#define MAX_DEPTH 96    /* bounds recursion (3 levels per parenthesis) */
#define MAX_NAME  8

typedef struct {
    const char   *p;
    double        x;
    int           depth;
    expr_status_t err;
} parser_t;

typedef double (*fn1_t)(double);
static const struct { const char *name; fn1_t fn; } FUNCS[] = {
    {"sin",  sin},  {"cos",  cos},  {"tan",  tan},
    {"asin", asin}, {"acos", acos}, {"atan", atan},
    {"sinh", sinh}, {"cosh", cosh}, {"tanh", tanh},
    {"sqrt", sqrt}, {"cbrt", cbrt}, {"exp",  exp},
    {"ln",   log},  {"log",  log10}, {"log10", log10},
    {"abs",  fabs},
};

static double parse_expr(parser_t *ps);
static double parse_unary(parser_t *ps);

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void skip_ws(parser_t *ps) { while (*ps->p == ' ') ps->p++; }

static double fail(parser_t *ps, expr_status_t e) {
    if (ps->err == EXPR_OK) ps->err = e;
    return NAN;
}

static bool enter(parser_t *ps) {
    if (++ps->depth > MAX_DEPTH) { fail(ps, EXPR_ERR_DEPTH); return false; }
    return true;
}

/* digits [. digits] [(e|E) [+|-] digits]; a lone "2e" is 2 times e */
static double parse_number(parser_t *ps) {
    const char *s = ps->p, *q = s;
    while (is_digit(*q)) q++;
    if (*q == '.') { q++; while (is_digit(*q)) q++; }
    if (*q == 'e' || *q == 'E') {
        const char *t = q + 1;
        if (*t == '+' || *t == '-') t++;
        if (is_digit(*t)) { q = t; while (is_digit(*q)) q++; }
    }
    char buf[48];
    size_t n = (size_t)(q - s);
    if (n == 0 || n >= sizeof(buf)) return fail(ps, EXPR_ERR_SYNTAX);
    memcpy(buf, s, n);
    buf[n] = 0;
    ps->p = q;
    return strtod(buf, NULL);
}

static double parse_primary(parser_t *ps) {
    skip_ws(ps);
    char c = *ps->p;
    if (c == '(') {
        ps->p++;
        double v = parse_expr(ps);
        skip_ws(ps);
        if (*ps->p != ')') return fail(ps, EXPR_ERR_PAREN);
        ps->p++;
        return v;
    }
    if (is_digit(c) || (c == '.' && is_digit(ps->p[1])))
        return parse_number(ps);
    if (is_alpha(c)) {
        const char *s = ps->p;
        while (is_alpha(*ps->p) || is_digit(*ps->p)) ps->p++;
        size_t n = (size_t)(ps->p - s);
        if (n == 1 && s[0] == 'x') return ps->x;
        if (n == 1 && s[0] == 'e') return M_E;
        if (n == 2 && strncmp(s, "pi", 2) == 0) return M_PI;
        for (size_t i = 0; i < sizeof(FUNCS) / sizeof(FUNCS[0]); i++) {
            if (strlen(FUNCS[i].name) == n && strncmp(s, FUNCS[i].name, n) == 0) {
                skip_ws(ps);
                if (*ps->p != '(') return fail(ps, EXPR_ERR_SYNTAX);
                ps->p++;
                double arg = parse_expr(ps);
                skip_ws(ps);
                if (*ps->p != ')') return fail(ps, EXPR_ERR_PAREN);
                ps->p++;
                return FUNCS[i].fn(arg);
            }
        }
        return fail(ps, EXPR_ERR_NAME);
    }
    if (c == ')') return fail(ps, EXPR_ERR_PAREN);
    return fail(ps, EXPR_ERR_SYNTAX);    /* includes end of input */
}

static double parse_power(parser_t *ps) {
    if (!enter(ps)) return NAN;
    double base = parse_primary(ps);
    skip_ws(ps);
    if (ps->err == EXPR_OK &&
        (*ps->p == '^' || (ps->p[0] == '*' && ps->p[1] == '*'))) {
        ps->p += (*ps->p == '^') ? 1 : 2;
        base = pow(base, parse_unary(ps));
    }
    ps->depth--;
    return base;
}

static double parse_unary(parser_t *ps) {
    if (!enter(ps)) return NAN;
    skip_ws(ps);
    double v;
    if (*ps->p == '-')      { ps->p++; v = -parse_unary(ps); }
    else if (*ps->p == '+') { ps->p++; v =  parse_unary(ps); }
    else                    v = parse_power(ps);
    ps->depth--;
    return v;
}

static double parse_term(parser_t *ps) {
    double v = parse_unary(ps);
    while (ps->err == EXPR_OK) {
        skip_ws(ps);
        char c = *ps->p;
        if (c == '*' && ps->p[1] != '*') { ps->p++; v *= parse_unary(ps); }
        else if (c == '/')               { ps->p++; v /= parse_unary(ps); }
        /* implicit multiplication: 2x, 2pi, 3(x+1), (x+1)(x-1), 2sin(x) */
        else if (c == '(' || is_alpha(c) || is_digit(c) || c == '.')
                                         { v *= parse_power(ps); }
        else break;
    }
    return v;
}

static double parse_expr(parser_t *ps) {
    if (!enter(ps)) return NAN;
    double v = parse_term(ps);
    while (ps->err == EXPR_OK) {
        skip_ws(ps);
        if (*ps->p == '+')      { ps->p++; v += parse_term(ps); }
        else if (*ps->p == '-') { ps->p++; v -= parse_term(ps); }
        else break;
    }
    ps->depth--;
    return v;
}

expr_status_t expr_eval(const char *src, double x, double *out) {
    parser_t ps = { src, x, 0, EXPR_OK };
    skip_ws(&ps);
    if (*ps.p == 0) return EXPR_ERR_EMPTY;
    double v = parse_expr(&ps);
    skip_ws(&ps);
    if (ps.err == EXPR_OK && *ps.p)
        ps.err = (*ps.p == ')') ? EXPR_ERR_PAREN : EXPR_ERR_SYNTAX;
    if (ps.err != EXPR_OK) return ps.err;
    *out = v;
    return EXPR_OK;
}

const char *expr_error(expr_status_t st) {
    switch (st) {
        case EXPR_OK:         return "";
        case EXPR_ERR_EMPTY:  return "lege invoer";
        case EXPR_ERR_SYNTAX: return "syntaxfout";
        case EXPR_ERR_PAREN:  return "haakjes kloppen niet";
        case EXPR_ERR_NAME:   return "onbekende naam";
        case EXPR_ERR_DEPTH:  return "te diep genest";
    }
    return "fout";
}

void expr_format(double v, char *buf, int len) {
    if (isnan(v))      { snprintf(buf, (size_t)len, "ongedefinieerd"); return; }
    if (isinf(v))      { snprintf(buf, (size_t)len, v > 0 ? "oneindig" : "-oneindig"); return; }
    if (v == 0.0) v = 0.0;          /* print -0 as 0 */
    snprintf(buf, (size_t)len, "%.10g", v);
}

const char *expr_key_text(key_code_t k, bool shift) {
    switch (k) {
        case KEY_0: return shift ? ")" : "0";
        case KEY_1: return "1";
        case KEY_2: return "2";
        case KEY_3: return "3";
        case KEY_4: return "4";
        case KEY_5: return "5";
        case KEY_6: return "6";
        case KEY_7: return "7";
        case KEY_8: return "8";
        case KEY_9: return shift ? "(" : "9";
        case KEY_DOT:   return ".";
        case KEY_EE:    return "E";
        case KEY_PLUS:  return "+";
        case KEY_MINUS: return "-";
        case KEY_MUL:   return "*";
        case KEY_DIV:   return "/";
        case KEY_POW:   return "^";
        case KEY_XNT:   return "x";
        case KEY_SQRT:  return shift ? "cbrt(" : "sqrt(";
        case KEY_SIN:   return shift ? "asin(" : "sin(";
        case KEY_COS:   return shift ? "acos(" : "cos(";
        case KEY_TAN:   return shift ? "atan(" : "tan(";
        case KEY_LN:    return shift ? "exp("  : "ln(";
        case KEY_LOG:   return shift ? "10^"   : "log(";
        case KEY_EXP:   return shift ? "e"     : "exp(";
        default:        return NULL;
    }
}
