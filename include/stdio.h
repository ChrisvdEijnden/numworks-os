#pragma once
#ifndef _STDIO_H
#define _STDIO_H
#include "stddef.h"
#include "stdarg.h"
/* Minimal hosted-free stdio — only formatting functions used in this project */
int  snprintf (char *buf, size_t size, const char *fmt, ...);
int  vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);
int  sprintf  (char *buf, const char *fmt, ...);
int  sscanf   (const char *buf, const char *fmt, ...);
/* printf routes to shell_putc via _write newlib stub */
int  printf   (const char *fmt, ...);
int  puts     (const char *s);
int  putchar  (int c);
/* FILE stubs — not used in bare-metal but needed for some headers */
typedef struct _FILE FILE;
extern FILE *stdout;
extern FILE *stderr;
int  fprintf  (FILE *stream, const char *fmt, ...);
int  fputs    (const char *s, FILE *stream);
#endif
