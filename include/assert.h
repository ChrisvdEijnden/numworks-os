#pragma once
#ifndef _ASSERT_H
#define _ASSERT_H
#ifdef NDEBUG
  #define assert(expr) ((void)0)
#else
  #define assert(expr) \
    ((expr) ? (void)0 : \
     (void)(*((volatile unsigned*)0xE000ED0C) = 0x05FA0004UL))
#endif
#endif
