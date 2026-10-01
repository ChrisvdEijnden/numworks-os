/* ================================================================
 * NumWorks OS — graph analysis: zeros and extremes
 * File: apps/common/analysis.c
 *
 * The interval is sampled at SAMPLES points. A zero is a sign change
 * between two samples (or a sample that is exactly 0), refined by
 * bisection; a sign change across a pole (1/x at 0) is rejected
 * because |f| doesn't shrink as the bracket does. An extremum is a
 * sample higher (or lower) than both neighbours, refined by
 * golden-section search between them. Intersections are zeros of
 * f - g (see the Functions app).
 * ================================================================ */
#include "analysis.h"
#include <math.h>

#define SAMPLES 1200

static bool finite_d(double v) { return !isnan(v) && !isinf(v); }

static bool bisect(an_fn_t f, void *ctx, double a, double fa, double b, double *root) {
    double start = fabs(fa) > fabs(f(ctx, b)) ? fabs(fa) : fabs(f(ctx, b));
    for (int i = 0; i < 100; i++) {
        double m = 0.5 * (a + b), fm = f(ctx, m);
        if (!finite_d(fm)) return false;
        if (fm == 0.0) { a = b = m; break; }
        if ((fa < 0) == (fm < 0)) { a = m; fa = fm; } else b = m;
        if (fabs(b - a) <= 1e-12 * (fabs(a) + fabs(b)) + 1e-300) break;
    }
    double r = 0.5 * (a + b), fr = f(ctx, r);
    /* At a real zero |f| ends far below where it started; across a
     * pole it grows instead */
    if (!finite_d(fr) || fabs(fr) > 1e-6 * (start > 1.0 ? start : 1.0)) return false;
    *root = r;
    return true;
}

bool an_next_zero(an_fn_t f, void *ctx, double x0, double x1, double *x) {
    if (!(x1 > x0)) return false;
    double h = (x1 - x0) / SAMPLES;
    double pa = x0, pf = f(ctx, x0);
    for (int i = 1; i <= SAMPLES; i++) {
        double xb = x0 + h * i, fb = f(ctx, xb);
        if (finite_d(fb) && fb == 0.0) { *x = xb; return true; }
        if (finite_d(pf) && finite_d(fb) && (pf < 0) != (fb < 0) &&
            bisect(f, ctx, pa, pf, xb, x) && *x > x0) return true;
        pa = xb; pf = fb;
    }
    return false;
}

bool an_next_extremum(an_fn_t f, void *ctx, double x0, double x1, bool maximum, double *x) {
    if (!(x1 > x0)) return false;
    double h = (x1 - x0) / SAMPLES, s = maximum ? 1.0 : -1.0;
    double fa = s * f(ctx, x0), fb = s * f(ctx, x0 + h);
    for (int i = 2; i <= SAMPLES; i++) {
        double xc = x0 + h * i, fc = s * f(ctx, xc);
        if (finite_d(fa) && finite_d(fb) && finite_d(fc) && fb >= fa && fb > fc) {
            /* golden-section search on [xc - 2h, xc] */
            const double g = 0.6180339887498949;
            double a = xc - 2 * h, b = xc;
            double c = b - g * (b - a), d = a + g * (b - a);
            double fcc = s * f(ctx, c), fdd = s * f(ctx, d);
            for (int k = 0; k < 80; k++) {
                if (fcc > fdd) { b = d; d = c; fdd = fcc; c = b - g * (b - a); fcc = s * f(ctx, c); }
                else           { a = c; c = d; fcc = fdd; d = a + g * (b - a); fdd = s * f(ctx, d); }
            }
            *x = 0.5 * (a + b);
            if (*x > x0) return true;
        }
        fa = fb; fb = fc;
    }
    return false;
}
