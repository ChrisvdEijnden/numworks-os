/* ================================================================
 * NumWorks OS — simulator: what the window and the simulated
 * hardware share.
 *
 * The OS runs unchanged in its own thread (sim_main.c starts it).
 * The window thread reads the panel, LED and backlight state below
 * and presses keys; the OS thread does everything else.
 * ================================================================ */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../hal/keyboard.h"

#define SIM_PANEL_W 320
#define SIM_PANEL_H 240

/* The OS thread's stack and MicroPython's heap (the firmware gets them
 * from the linker script) */
#define SIM_STACK_SIZE 1048576
#define SIM_MP_HEAP    49152

/* ── Time (sim_hw.c) ──────────────────────────────────────────── */
uint64_t sim_now_us(void);
void     sim_wfi(void);                 /* the kernel's WFI */

/* ── Screen (sim_display.c): the ST7789V's frame memory ─────────── */
/* Fills out[] with what the panel shows (black when it is off or
 * asleep), as 0xRRGGBB */
void sim_panel_read(uint32_t out[SIM_PANEL_H][SIM_PANEL_W]);

/* ── Keys (sim_keyboard.c) ────────────────────────────────────── */
void sim_key(key_code_t k, bool down);
bool sim_key_down(key_code_t k);

/* ── LED and backlight (sim_hw.c) ─────────────────────────────── */
uint32_t sim_led_rgb(void);             /* 0xRRGGBB, 0 when off */
int      sim_backlight(void);           /* 0..BACKLIGHT_MAX, -1 when off */

/* ── Debug UART (sim_hw.c): stdout, and stdin or script input ──── */
void sim_uart_type(const char *s);      /* window thread */

/* ── Storage and PC transfer (sim_hw.c) ───────────────────────── */
bool        sim_storage_open(const char *path, bool erase);
void        sim_storage_sync(void);
void        sim_usb_enable(bool on);    /* before the OS starts */
const char *sim_usb_port(void);         /* the pseudo-terminal, or NULL */

/* ── Reboot (sim_main.c): hal_reset() and the crash screen ──────── */
__attribute__((noreturn)) void sim_reboot(void);
