#pragma once
#include <stdbool.h>

/* Statistics on lists of numbers (apps/common/stats.c) */
#define STATS_MAX 100

typedef struct {
    int    n;
    double sum, mean, min, max, range;
    double median, q1, q3;          /* quartiles: medians of the lower and upper
                                       halves, the median itself left out when
                                       n is odd (as on the TI-84) */
    double var_pop, sd_pop;         /* population: divide by n */
    double sd_sample;               /* sample: divide by n - 1 (0 if n < 2) */
} stats1_t;

typedef struct {
    double a, b;                    /* y = a*x + b, least squares */
    double r, r2;                   /* correlation; NaN if all y are equal */
} linreg_t;

bool stats1(const double *x, int n, stats1_t *out);              /* false if n < 1 */
bool stats_linreg(const double *x, const double *y, int n, linreg_t *out);  /* false if n < 2 or all x equal */
