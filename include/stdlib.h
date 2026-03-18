#pragma once
#ifndef _STDLIB_H
#define _STDLIB_H
#include "stddef.h"
/* Conversion */
int       atoi  (const char *s);
long      atol  (const char *s);
double    atof  (const char *s);
long      strtol (const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
float     strtof (const char *s, char **endptr);
double    strtod (const char *s, char **endptr);
/* Memory — provided by newlib via _sbrk stub */
void *malloc (size_t size);
void *calloc (size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free   (void *ptr);
/* Misc */
void  abort  (void);
int   abs    (int x);
long  labs   (long x);
int   rand   (void);
void  srand  (unsigned int seed);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void  exit   (int status);
#endif
