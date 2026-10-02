/* ================================================================
 * NumWorks OS — simulator: the hardware below the drivers
 *
 * Stand-ins for what the firmware gets from the chip: SysTick, delays
 * and the watchdog (hal/hal.c), the debug UART (stdin/stdout), clocks,
 * LED, backlight, battery, the QSPI flash (a file) and the USB core
 * (a pseudo-terminal, so tools/upload.py can talk to the simulator).
 * The display and keyboard drivers are the real ones, on simulated
 * pins (sim_display.c, sim_keyboard.c).
 *
 * Everything here runs in the OS thread, except the sim_*() accessors
 * the window calls.
 * ================================================================ */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "sim.h"
#include "../hal/hal.h"
#include "../hal/led.h"
#include "../hal/backlight.h"
#include "../hal/battery.h"
#include "../hal/clocks.h"
#include "../hal/uart.h"
#include "../fs/storage.h"
#include "../usb/usb_device.h"
#include "../usb/usb_cdc.h"
#include "../include/config.h"

_Static_assert(SIM_MP_HEAP == MP_HEAP_SIZE, "sim.h: SIM_MP_HEAP must match MP_HEAP_SIZE");

/* ── Memory regions the linker script provides on the calculator ──
 * The OS thread runs on _sstack (sim_main.c sets that up); MicroPython's
 * heap is _smp_heap. Their ends are pointers (mp_port.c, built with
 * HOST_REGIONS): macOS's linker can't put a symbol at the end of another. */
uint8_t _sstack[SIM_STACK_SIZE] __attribute__((aligned(16384)));
uint8_t _smp_heap[SIM_MP_HEAP] __attribute__((aligned(16)));
uint8_t *const host_estack = _sstack + SIM_STACK_SIZE;
uint8_t *const host_emp_heap = _smp_heap + SIM_MP_HEAP;

/* ── Time: SysTick ────────────────────────────────────────────────
 * Ticks are delivered in the OS thread, never from another thread, so
 * the kernel sees them exactly as between instructions on the chip:
 * whenever it asks the time, waits, or sleeps in WFI, the ticks that
 * are due are run first. */
volatile uint32_t g_tick_ms = 0;
volatile uint32_t g_tick_step = 1;
volatile uint32_t g_hclk_hz = 192000000U;
bool g_boot_hse = true;
void SysTick_Handler(void);             /* kernel/kernel.c */

static uint64_t s_next_tick;            /* when the next tick is due (µs) */

uint64_t sim_now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000U + (uint64_t)t.tv_nsec / 1000U;
}

static void sleep_us(uint64_t us) {
    struct timespec t = { (time_t)(us / 1000000U), (long)(us % 1000000U) * 1000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {}
}

static void ticks(void) {
    uint64_t now = sim_now_us(), period = (uint64_t)g_tick_step * 1000U;
    if (s_next_tick == 0) s_next_tick = now + period;
    if (now < s_next_tick) return;
    uint64_t due = (now - s_next_tick) / period + 1;
    if (due > 100) {                    /* the process was stopped: catch up at once */
        g_tick_ms += (uint32_t)(due - 1) * g_tick_step;
        due = 1;
    }
    while (due--) SysTick_Handler();
    s_next_tick += ((now - s_next_tick) / period + 1) * period;
}

void sim_wfi(void) {
    uint64_t now = sim_now_us();
    if (s_next_tick > now) sleep_us(s_next_tick - now);
    ticks();
}

uint32_t hal_tick_ms(void) { ticks(); return g_tick_ms; }

uint32_t hal_tick_us(void) {
    ticks();
    uint64_t period = (uint64_t)g_tick_step * 1000U, now = sim_now_us();
    uint64_t into = now + period > s_next_tick ? now + period - s_next_tick : 0;
    return g_tick_ms * 1000U + (uint32_t)(into < period ? into : period - 1);
}

void hal_delay_ms(uint32_t ms) {
    uint64_t end = sim_now_us() + (uint64_t)ms * 1000U;
    for (;;) {
        ticks();
        uint64_t now = sim_now_us();
        if (now >= end) return;
        uint64_t wake = s_next_tick < end ? s_next_tick : end;
        sleep_us(wake > now ? wake - now : 0);
    }
}

/* Short waits are for the hardware to settle; the simulated pins
 * settle at once */
void hal_delay_us(uint32_t us) {
    if (us >= 1000) hal_delay_ms(us / 1000);
    else ticks();
}

void hal_tick_set_period(uint32_t ms) {
    if (ms < 1) ms = 1;
    if (ms > 80) ms = 80;
    ticks();
    g_tick_step = ms;
    s_next_tick = sim_now_us() + (uint64_t)ms * 1000U;
}

/* ── hal/hal.c ────────────────────────────────────────────────── */
void hal_init(void) {
    hal_uart_init();
    hal_uart_puts("\r\nNumWorks OS v" NWOS_VERSION " booting... (simulator)\r\n");
}

void hal_watchdog_start(void) {}
void hal_watchdog_feed(void) {}
bool hal_reset_by_watchdog(void) { return false; }

void hal_boot_log(const char *stage) {
    char line[160];
    snprintf(line, sizeof line, "[boot %5lu ms] %s\n", (unsigned long)g_tick_ms, stage);
    hal_uart_puts(line);
}

#define STACK_PAINT 0x5A7AC4EDUL

void hal_stack_paint(void) {            /* sim_main.c, before the OS thread starts */
    uint32_t *p = (uint32_t *)(void *)_sstack;
    for (size_t i = 0; i < SIM_STACK_SIZE / 4; i++) p[i] = STACK_PAINT;
}

void hal_stack_stats(uint32_t *peak, uint32_t *size) {
    const uint32_t *p = (const uint32_t *)(const void *)_sstack, *end = p + SIM_STACK_SIZE / 4;
    while (p < end && *p == STACK_PAINT) p++;
    if (peak) *peak = (uint32_t)((end - p) * sizeof(uint32_t));
    if (size) *size = SIM_STACK_SIZE;
}

/* The C heap is the PC's own here */
void hal_heap_stats(uint32_t *used, uint32_t *total) {
    if (used) *used = 0;
    if (total) *total = 0;
}

/* ── Debug UART: stdout and stdin ───────────────────────────────
 * Script steps can also type into it (sim_uart_type, window thread):
 * a ring with one writer and one reader. */
static bool s_stdin_open = true;
static char s_typed[1024];
static unsigned s_typed_head, s_typed_tail;

void sim_uart_type(const char *s) {
    for (; *s; s++) {
        unsigned t = s_typed_tail, next = (t + 1) % sizeof s_typed;
        if (next == __atomic_load_n(&s_typed_head, __ATOMIC_ACQUIRE)) return;   /* full */
        s_typed[t] = *s;
        __atomic_store_n(&s_typed_tail, next, __ATOMIC_RELEASE);
    }
}

void hal_uart_init(void) { setvbuf(stdout, NULL, _IOLBF, 0); }
void hal_uart_putc(char c) { if (c != '\r') putchar(c); }
void hal_uart_puts(const char *s) { while (*s) hal_uart_putc(*s++); }
void hal_uart_flush(void) { fflush(stdout); }

static bool typed_waiting(void) {
    return s_typed_head != __atomic_load_n(&s_typed_tail, __ATOMIC_ACQUIRE);
}

int hal_uart_available(void) {
    if (typed_waiting()) return 1;
    if (!s_stdin_open) return 0;
    struct pollfd p = { 0, POLLIN, 0 };
    return poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP));
}

int hal_uart_getc(void) {
    if (typed_waiting()) {
        int c = (unsigned char)s_typed[s_typed_head];
        __atomic_store_n(&s_typed_head, (s_typed_head + 1) % sizeof s_typed, __ATOMIC_RELEASE);
        return c;
    }
    if (!hal_uart_available()) return -1;
    unsigned char c;
    if (read(0, &c, 1) != 1) { s_stdin_open = false; return -1; }   /* end of input */
    return c;
}

/* ── Clocks ───────────────────────────────────────────────────── */
void clocks_high(void) {}
void clocks_low(void) {}

/* ── LED: colours as in hal/led.c ─────────────────────────────── */
static const uint32_t LED_COLOURS[LED_COLOUR_COUNT] = { 0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF };
static volatile led_colour_t s_led = LED_OFF;
static volatile led_charge_t s_charge = LED_CHARGE_NONE;
static volatile bool s_led_suspended;

void led_init(void) {}
bool led_available(void) { return true; }
void led_set(led_colour_t c) { if (c < LED_COLOUR_COUNT) s_led = c; }
led_colour_t led_get(void) { return s_led; }
void led_set_charge(led_charge_t state) { s_charge = state; }
void led_suspend(void) { s_led_suspended = true; }
void led_resume(void) { s_led_suspended = false; }

uint32_t sim_led_rgb(void) {
    if (s_charge == LED_CHARGE_CHARGING) return 0xFF3C00;
    if (s_charge == LED_CHARGE_FULL) return 0x00FF00;
    return s_led_suspended ? 0 : LED_COLOURS[s_led];
}

/* ── Backlight ────────────────────────────────────────────────── */
static volatile uint8_t s_level = 12;   /* hal/backlight.c's DEFAULT_LEVEL */
static volatile bool s_backlight_on;

void backlight_init(void) { s_backlight_on = true; }
void backlight_set_level(uint8_t level) { s_level = level > BACKLIGHT_MAX ? BACKLIGHT_MAX : level; }
uint8_t backlight_level(void) { return s_level; }
void backlight_power(bool on) { s_backlight_on = on; }
int sim_backlight(void) { return s_backlight_on ? s_level : -1; }

/* ── Battery: full, not on USB ────────────────────────────────── */
static bool s_battery_reported;

void battery_init(void) {}
uint32_t battery_mv(void) { return 4100; }
bool battery_charging(void) { return false; }
bool battery_usb_powered(void) { return false; }
battery_level_t battery_level(void) { return BAT_FULL; }
bool battery_poll(void) {
    if (s_battery_reported) return false;
    s_battery_reported = true;
    return true;
}

/* ── QSPI flash: the file system area, kept in a file ──────────────
 * Same rules as the chip: erasing sets 4 KB sectors to FF, programming
 * can only clear bits. */
static uint8_t *s_flash;
static char s_flash_path[1024];
static char s_flash_status[1100];

bool sim_storage_open(const char *path, bool erase) {
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) { perror(path); return false; }
    struct stat st;
    bool fresh = fstat(fd, &st) == 0 && st.st_size != (off_t)STORAGE_SIZE;
    if (fresh && ftruncate(fd, STORAGE_SIZE) != 0) { perror(path); close(fd); return false; }
    s_flash = mmap(NULL, STORAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (s_flash == MAP_FAILED) { s_flash = NULL; perror(path); return false; }
    if (fresh || erase) memset(s_flash, 0xFF, STORAGE_SIZE);   /* a new chip is erased */
    snprintf(s_flash_path, sizeof s_flash_path, "%s", path);
    return true;
}

void sim_storage_sync(void) {
    if (s_flash) msync(s_flash, STORAGE_SIZE, MS_SYNC);
}

bool storage_init(void) { return s_flash != NULL; }

const char *storage_status(void) {
    if (!s_flash) return "simulator: no storage file";
    snprintf(s_flash_status, sizeof s_flash_status, "ok (simulator: %s)", s_flash_path);
    return s_flash_status;
}

const uint8_t *storage_base(void) { return s_flash; }
uint32_t storage_size(void) { return STORAGE_SIZE; }

int storage_erase(uint32_t off, uint32_t len) {
    if (!s_flash || off % STORAGE_ERASE_SIZE || len % STORAGE_ERASE_SIZE ||
        off > STORAGE_SIZE || len > STORAGE_SIZE - off) return -1;
    memset(s_flash + off, 0xFF, len);
    return 0;
}

int storage_program(uint32_t off, const void *src, uint32_t len) {
    if (!s_flash || off > STORAGE_SIZE || len > STORAGE_SIZE - off) return -1;
    const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) s_flash[off + i] &= s[i];
    return 0;
}

/* ── USB: a pseudo-terminal in place of the CDC serial port ─────────
 * usb/usb_cdc.c (the transfer protocol) is the real one; this moves
 * its bytes to and from the pseudo-terminal, which tools/upload.py
 * opens like the calculator's serial port. */
static bool s_usb_wanted = true;
static int s_pty = -1;
static char s_pty_name[128];
static uint8_t s_rx[256], s_tx[256];
static int s_rx_n, s_rx_off, s_tx_n, s_tx_off;

void sim_usb_enable(bool on) { s_usb_wanted = on; }
const char *sim_usb_port(void) { return s_pty >= 0 ? s_pty_name : NULL; }

void usb_device_init(void) {
    if (!s_usb_wanted || s_pty >= 0) return;
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0 || !ptsname(m)) {
        if (m >= 0) close(m);
        hal_uart_puts("usb: no pseudo-terminal, PC transfer off\n");
        return;
    }
    snprintf(s_pty_name, sizeof s_pty_name, "%s", ptsname(m));
    fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
    s_pty = m;
    char line[256];
    snprintf(line, sizeof line, "usb: PC transfer on %s, e.g. python3 tools/upload.py --port %s list\n",
             s_pty_name, s_pty_name);
    hal_uart_puts(line);
}

void usb_device_poll(void) {
    if (s_pty < 0) return;
    if (s_rx_n == 0) {
        ssize_t n = read(s_pty, s_rx, sizeof s_rx);
        if (n > 0) { s_rx_n = (int)n; s_rx_off = 0; }
    }
    if (s_rx_n) {
        int t = usb_cdc_rx_push(s_rx + s_rx_off, s_rx_n);
        s_rx_off += t;
        s_rx_n -= t;
    }
    /* What the PC doesn't take now waits here, as with a host that
     * stops reading: the protocol's ring fills up and it waits */
    if (s_tx_n == 0) { s_tx_n = usb_cdc_tx_pop(s_tx, sizeof s_tx); s_tx_off = 0; }
    while (s_tx_n > 0) {
        ssize_t r = write(s_pty, s_tx + s_tx_off, (size_t)s_tx_n);
        if (r <= 0) break;
        s_tx_off += (int)r;
        s_tx_n -= (int)r;
    }
}

bool usb_device_configured(void) { return s_pty >= 0; }
void usb_device_irq(void) {}

/* ── MicroPython: an error outside every handler ──────────────────
 * The firmware links with --wrap=nlr_jump_fail (macOS's linker has no
 * --wrap); the embed port's own version, renamed when it is compiled,
 * would hang. mp_port.c's shows the crash screen. */
#ifdef NWOS_SIM_MICROPYTHON
__attribute__((noreturn)) void __wrap_nlr_jump_fail(void *val);
__attribute__((noreturn)) void nlr_jump_fail(void *val) { __wrap_nlr_jump_fail(val); }
#endif
