#pragma once
#ifndef _MATH_H
#define _MATH_H
/* ARM GCC provides hardware FP built-ins for all of these */
double sin  (double x);
double cos  (double x);
double tan  (double x);
double asin (double x);
double acos (double x);
double atan (double x);
double atan2(double y, double x);
double sinh (double x);
double cosh (double x);
double tanh (double x);
double exp  (double x);
double log  (double x);
double log10(double x);
double pow  (double x, double y);
double sqrt (double x);
double cbrt (double x);
double fabs (double x);
double floor(double x);
double ceil (double x);
double fmod (double x, double y);
double round(double x);
float  sinf (float x);
float  cosf (float x);
float  tanf (float x);
float  sqrtf(float x);
float  fabsf(float x);
float  floorf(float x);
float  ceilf(float x);
float  powf (float x, float y);
float  logf (float x);
float  log10f(float x);
float  expf (float x);
float  roundf(float x);
#define HUGE_VAL  __builtin_huge_val()
#define INFINITY  __builtin_inff()
#define NAN       __builtin_nanf("")
#define M_PI      3.14159265358979323846
#define M_E       2.71828182845904523536
static inline int isinf(double x) { return __builtin_isinf(x); }
static inline int isnan(double x) { return __builtin_isnan(x); }
#endif
