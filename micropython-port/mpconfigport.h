/* ================================================================
 * MicroPython configuration for NumWorks OS (embed port)
 * Used by `make mp` (micropython_embed.mk) and by the firmware build.
 * ================================================================ */
#include <port/mpconfigport_common.h>

#define MICROPY_CONFIG_ROM_LEVEL        (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER         (1)
#define MICROPY_ENABLE_GC               (1)
#define MICROPY_HELPER_REPL             (1)   /* REPL lines echo their value */
#define MICROPY_STACK_CHECK             (1)

/* Single precision matches the STM32F730's FPU (fpv5-sp-d16) */
#define MICROPY_FLOAT_IMPL              (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_PY_MATH                 (1)
#define MICROPY_PY_IO                   (1)   /* io.StringIO, read-only open() */
#define MICROPY_PY_BUILTINS_STR_SPLITLINES (1)
#define MICROPY_PY_SYS                  (1)
#define MICROPY_PY_SYS_PLATFORM         "numworks"
#define MICROPY_PY_BUILTINS_INPUT       (1)   /* input() reads a line from the keypad */
#define MICROPY_ENABLE_EXTERNAL_IMPORT  (1)   /* `import x` loads x.py from flash */
/* `time` and `random` are our own modules (modules/nwos/), since the
 * embed port doesn't ship MicroPython's extmod ones */

/* BACK (or HOME) interrupts a running script with KeyboardInterrupt:
 * every MICROPY_VM_HOOK_COUNT jumps/returns the VM calls nwos_mp_poll(). */
#define MICROPY_KBD_EXCEPTION           (1)
#define MICROPY_VM_HOOK_COUNT           (64)
#define MICROPY_VM_HOOK_INIT            static unsigned int vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL            if (--vm_hook_divisor == 0) { \
                                            vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
                                            extern void nwos_mp_poll(void); \
                                            nwos_mp_poll(); \
                                        }
#define MICROPY_VM_HOOK_LOOP            MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN          MICROPY_VM_HOOK_POLL
