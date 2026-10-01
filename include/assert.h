#pragma once
#ifndef _ASSERT_H
#define _ASSERT_H
#ifdef NDEBUG
  #define assert(expr) ((void)0)
#else
  __attribute__((noreturn)) void hal_panic(const char *msg);
  #define NWOS_STR2(x) #x
  #define NWOS_STR(x) NWOS_STR2(x)
  #define assert(expr) \
    ((expr) ? (void)0 : hal_panic("assert " __FILE__ ":" NWOS_STR(__LINE__)))
#endif
#endif
