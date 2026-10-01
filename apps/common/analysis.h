#pragma once
#include <stdbool.h>

/* Searches on a function of one variable (apps/common/analysis.c).
 * Each looks in (x0, x1], left to right, and returns the first hit
 * nearest to x0; f may be NaN or infinite outside its domain. */
typedef double (*an_fn_t)(void *ctx, double x);

bool an_next_zero(an_fn_t f, void *ctx, double x0, double x1, double *x);
bool an_next_extremum(an_fn_t f, void *ctx, double x0, double x1, bool maximum, double *x);
