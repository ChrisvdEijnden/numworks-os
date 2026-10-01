/* ================================================================
 * NumWorks OS — statistics: one-variable summary, linear regression
 * File: apps/common/stats.c
 * ================================================================ */
#include "stats.h"
#include <math.h>
#include <string.h>

static void sort(double *v, int n) {             /* insertion sort: n <= 100 */
    for (int i = 1; i < n; i++) {
        double t = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > t) { v[j + 1] = v[j]; j--; }
        v[j + 1] = t;
    }
}

static double median_of(const double *v, int n) {
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

bool stats1(const double *x, int n, stats1_t *o) {
    if (n < 1 || n > STATS_MAX) return false;
    double v[STATS_MAX];
    memcpy(v, x, (size_t)n * sizeof(double));
    sort(v, n);
    memset(o, 0, sizeof(*o));
    o->n = n;
    for (int i = 0; i < n; i++) o->sum += v[i];
    o->mean = o->sum / n;
    o->min = v[0];
    o->max = v[n - 1];
    o->range = o->max - o->min;
    o->median = median_of(v, n);
    int half = n / 2;                            /* lower half: v[0..half-1] */
    if (n == 1) { o->q1 = o->q3 = v[0]; }
    else {
        o->q1 = median_of(v, half);
        o->q3 = median_of(v + (n - half), half);
    }
    double ss = 0;                               /* two passes: no cancellation */
    for (int i = 0; i < n; i++) ss += (v[i] - o->mean) * (v[i] - o->mean);
    o->var_pop = ss / n;
    o->sd_pop = sqrt(o->var_pop);
    o->sd_sample = n > 1 ? sqrt(ss / (n - 1)) : 0.0;
    return true;
}

bool stats_linreg(const double *x, const double *y, int n, linreg_t *o) {
    if (n < 2) return false;
    double mx = 0, my = 0;
    for (int i = 0; i < n; i++) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;
    double sxx = 0, syy = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        double dx = x[i] - mx, dy = y[i] - my;
        sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
    }
    if (sxx == 0.0) return false;                /* vertical: no y = ax + b */
    o->a = sxy / sxx;
    o->b = my - o->a * mx;
    if (syy == 0.0) {                            /* all y equal: r is 0/0 */
        o->r = o->r2 = NAN;
        return true;
    }
    o->r = sxy / sqrt(sxx * syy);
    if (o->r > 1.0) o->r = 1.0;                  /* rounding */
    if (o->r < -1.0) o->r = -1.0;
    o->r2 = o->r * o->r;
    return true;
}
