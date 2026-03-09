/* ================================================================
 * MicroPython port configuration for NumWorks OS
 * Tune these to control binary size vs. features.
 * ================================================================ */
#pragma once
#include <stdint.h>

/* MCU word size */
#define MICROPY_OBJ_REPR           MICROPY_OBJ_REPR_C
#define MICROPY_FLOAT_IMPL         MICROPY_FLOAT_IMPL_NONE  /* Save 8 KB */

/* Heap / stack */
#define MICROPY_STACK_CHECK        (1)
#define MICROPY_ENABLE_GC          (1)
#define MICROPY_GC_ALLOC_THRESHOLD (1)

/* Language features */
#define MICROPY_ENABLE_COMPILER    (1)
#define MICROPY_REPL_EVENT_DRIVEN  (0)
#define MICROPY_LONGINT_IMPL       MICROPY_LONGINT_IMPL_MPZ
#define MICROPY_PY_BUILTINS_COMPLEX (0)  /* Save ~3 KB */
#define MICROPY_PY_BUILTINS_FROZENSET (0)

/* Built-in modules to INCLUDE */
#define MICROPY_PY_SYS             (1)
#define MICROPY_PY_UTIME           (1)
#define MICROPY_PY_USTRUCT         (1)
#define MICROPY_PY_UOS             (1)

/* Built-in modules to EXCLUDE (saves flash) */
#define MICROPY_PY_USSL            (0)
#define MICROPY_PY_UHASHLIB        (0)
#define MICROPY_PY_UBINASCII       (0)
#define MICROPY_PY_URE             (0)
#define MICROPY_PY_UHEAPQ          (0)
#define MICROPY_PY_USELECT         (0)
#define MICROPY_PY_WEBSOCKET       (0)
#define MICROPY_PY_NETWORK         (0)
#define MICROPY_PY_BLUETOOTH       (0)
#define MICROPY_PY_LWIP            (0)
#define MICROPY_PY_WEBREPL         (0)

/* Types */
typedef int       mp_int_t;
typedef unsigned  mp_uint_t;
typedef long      mp_off_t;

/* Stdout */
#define MICROPY_PORT_INIT_FUNC      mp_init_port()
#define mp_type_fileio              mp_type_object
#define mp_type_textio              mp_type_object

#include <alloca.h>
