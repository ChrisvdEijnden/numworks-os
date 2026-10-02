/* ================================================================
 * NumWorks OS — simulator: the crash screen and reset
 *
 * hal/fault.c is the real one (a copy made by tests/gen_host.py, with
 * the system control registers as variables). hal_panic() shows its
 * report on the simulated screen and waits for a key; the reset that
 * follows, like hal_reset() from Settings or the shell, restarts the
 * simulator. The files stay.
 * ================================================================ */
#include <stdint.h>
#include <string.h>
#include "../hal/fault.h"
#include "../hal/hal.h"
#include "../hal/uart.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "sim.h"

static __attribute__((unused)) uint32_t host_aircr;   /* only the reset request writes it */
static uint32_t host_cfsr, host_hfsr, host_mmfar, host_bfar;
static void host_barrier(void) {}
static void host_reset(void) { sim_reboot(); }
extern uint32_t _sstack[];                      /* sim_hw.c */
#define _estack (_sstack + SIM_STACK_SIZE / 4)
extern volatile uint32_t g_tick_ms;

#include "fault_host.c"
