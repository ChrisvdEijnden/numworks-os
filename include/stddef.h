/* Bare-metal stddef.h for ARM Cortex-M bare-metal builds */
#pragma once
#ifndef _STDDEF_H
#define _STDDEF_H
typedef unsigned int  size_t;
typedef int           ptrdiff_t;
typedef long          ssize_t;
#ifndef NULL
  #define NULL ((void*)0)
#endif
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
