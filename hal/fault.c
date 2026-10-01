/* ================================================================
 * NumWorks OS — Crash reporting
 * File: hal/fault.c
 *
 * CPU faults (and unexpected interrupts) land in Fault_Handler in the
 * startup code, which hands the stacked registers to fault_report().
 * The report goes to the debug UART first (it needs the least to
 * work), then to the screen; a key press resets. Nothing here uses
 * interrupts, SysTick or the heap.
 * ================================================================ */
#include "fault.h"
#include "../ui/lang.h"
#include "hal.h"
#include "uart.h"
#include "display.h"
#include "keyboard.h"
#include <string.h>

#define SCB_AIRCR (*(volatile uint32_t *)0xE000ED0CUL)
#define SCB_CFSR  (*(volatile uint32_t *)0xE000ED28UL)
#define SCB_HFSR  (*(volatile uint32_t *)0xE000ED2CUL)
#define SCB_MMFAR (*(volatile uint32_t *)0xE000ED34UL)
#define SCB_BFAR  (*(volatile uint32_t *)0xE000ED38UL)

#define CFSR_MMARVALID (1U << 7)
#define CFSR_BFARVALID (1U << 15)

extern uint32_t _sstack[], _estack[];
extern volatile uint32_t g_tick_ms;

void hal_reset(void) {
    __asm volatile("dsb" ::: "memory");
    SCB_AIRCR = (0x5FAUL << 16) | (1U << 2);     /* SYSRESETREQ */
    __asm volatile("dsb" ::: "memory");
    for (;;) {}
}

/* ── Report text ────────────────────────────────────────────────── */
#define MAX_LINES 11
#define LINE_LEN  44          /* characters that fit on screen at x=10 */
static char s_lines[MAX_LINES][LINE_LEN + 1];
static int  s_nlines;

/* a then b on one line; text that doesn't fit continues on the next */
static void add_line(const char *a, const char *b) {
    const char *parts[2] = { a, b };
    if (s_nlines >= MAX_LINES) return;
    char *l = s_lines[s_nlines++];
    size_t n = 0;
    for (int i = 0; i < 2; i++) {
        for (const char *p = parts[i]; p && *p; p++) {
            if (n == LINE_LEN) {
                l[n] = 0;
                if (s_nlines >= MAX_LINES) return;
                l = s_lines[s_nlines++];
                l[0] = l[1] = ' ';
                n = 2;
            }
            l[n++] = *p;
        }
    }
    l[n] = 0;
}

static void add_hex(const char *label, uint32_t v) {
    char h[11] = "0x";
    for (int i = 9; i >= 2; i--) { h[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
    h[10] = 0;
    add_line(label, h);
}

static void add_dec(const char *label, uint32_t v) {
    char d[11];
    int i = 10;
    d[i] = 0;
    do { d[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    add_line(label, d + i);
}

/* Busy wait: SysTick can't interrupt a fault handler. Rough, but only
 * used for key debouncing. */
static void spin_ms(uint32_t ms) {
    hal_watchdog_feed();          /* the report stays up until a key */
    for (volatile uint32_t i = 0; i < ms * 20000U; i++) {}
}

static void wait_key(void) {
    int n = 0;
    while (n < 20) { n = keyboard_raw_any() ? 0 : n + 1; spin_ms(5); }  /* released */
    n = 0;
    while (n < 4)  { n = keyboard_raw_any() ? n + 1 : 0; spin_ms(5); }  /* pressed */
}

__attribute__((noreturn)) static void show_and_reset(void) {
    hal_uart_puts("\n*** NumWorks OS crash ***\n");
    for (int i = 0; i < s_nlines; i++) { hal_uart_puts(s_lines[i]); hal_uart_puts("\n"); }

    if (display_ready()) {
        const uint16_t bg = RGB(60, 0, 0), hdr = RGB(200, 30, 30);
        display_fill(bg);
        display_fill_rect(0, 0, LCD_WIDTH, 24, hdr);
        display_str(8, 8, TR("Systeemfout", "System error"), WHITE, hdr);
        for (int i = 0; i < s_nlines; i++)
            display_str(10, (int16_t)(32 + i * 14), s_lines[i], i == 0 ? YELLOW : WHITE, bg);
        display_str(10, LCD_HEIGHT - 28, TR("Druk op een toets om te herstarten.", "Press a key to restart."), WHITE, bg);
        display_str(10, LCD_HEIGHT - 14, TR("Bestanden blijven bewaard.", "Your files are kept."), RGB(255,180,180), bg);
        display_flush();
    }
    wait_key();
    hal_reset();
}

/* A fault while reporting one: don't try again, just reset */
static void enter(void) {
    static volatile int s_depth = 0;
    __asm volatile("cpsid i" ::: "memory");
    if (s_depth++) hal_reset();
    s_nlines = 0;
}

void hal_panic(const char *msg) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    enter();
    add_line(TR("Fout: ", "Error: "), msg);
    add_hex(TR("Aanroeper: ", "Caller: "), (uint32_t)caller);
    add_dec(TR("Tijd sinds start (ms): ", "Time since start (ms): "), g_tick_ms);
    show_and_reset();
}

/* Most likely cause, from the configurable fault status register */
static const char *cause(uint32_t cfsr) {
    static const struct { uint32_t bit; const char *nl, *en; } T[] = {
        { 1U << 12, "stack vol (BusFault bij stacken)", "stack full (BusFault while stacking)" },
        { 1U << 4,  "stack vol (MemManage bij stacken)", "stack full (MemManage while stacking)" },
        { 1U << 25, "deling door nul", "division by zero" },
        { 1U << 24, "niet-uitgelijnde toegang", "unaligned access" },
        { 1U << 16, "ongeldige instructie", "invalid instruction" },
        { 1U << 17, "ongeldige status (Thumb-bit)", "invalid state (Thumb bit)" },
        { 1U << 18, "ongeldige terugkeer uit interrupt", "invalid return from interrupt" },
        { 1U << 19, "coprocessor (FPU) uitgeschakeld", "coprocessor (FPU) disabled" },
        { 1U << 9,  "ongeldig geheugenadres", "invalid memory address" },
        { 1U << 10, "ongeldig adres (uitgesteld)", "invalid address (imprecise)" },
        { 1U << 8,  "code ophalen mislukt", "instruction fetch failed" },
        { 1U << 1,  "geheugenbescherming (data)", "memory protection (data)" },
        { 1U << 0,  "geheugenbescherming (code)", "memory protection (code)" },
        { 1U << 11, "fout bij terugzetten stack", "error restoring the stack" },
        { 1U << 3,  "fout bij terugzetten stack", "error restoring the stack" },
    };
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++)
        if (cfsr & T[i].bit) return TR(T[i].nl, T[i].en);
    return NULL;
}

void fault_report(const uint32_t *frame, uint32_t ipsr) {
    uint32_t cfsr = SCB_CFSR, hfsr = SCB_HFSR;
    enter();

    uint32_t exc = ipsr & 0x1FF;
    switch (exc) {
        case 3:  add_line("HardFault", NULL); break;
        case 4:  add_line(TR("MemManage-fout", "MemManage fault"), NULL); break;
        case 5:  add_line("BusFault", NULL); break;
        case 6:  add_line("UsageFault", NULL); break;
        default:
            if (exc >= 16) add_dec(TR("Onverwachte interrupt: IRQ ", "Unexpected interrupt: IRQ "), exc - 16);
            else           add_dec(TR("Onverwachte exceptie: ", "Unexpected exception: "), exc);
    }
    const char *why = cause(cfsr);
    if (why) add_line(TR("Oorzaak: ", "Cause: "), why);

    /* The stacked frame is r0-r3, r12, lr, pc, xpsr. If the stack had
     * overflowed it may not exist, so check before reading it. */
    if (frame >= _sstack && frame + 8 <= _estack) {
        add_hex("PC:   ", frame[6]);
        add_hex("LR:   ", frame[5]);
    } else {
        add_hex(TR("SP buiten de stack: ", "SP outside the stack: "), (uint32_t)(uintptr_t)frame);
    }
    add_hex("CFSR: ", cfsr);
    add_hex("HFSR: ", hfsr);
    if (cfsr & CFSR_BFARVALID) add_hex(TR("Adres: ", "Address: "), SCB_BFAR);
    else if (cfsr & CFSR_MMARVALID) add_hex(TR("Adres: ", "Address: "), SCB_MMFAR);
    add_dec(TR("Tijd sinds start (ms): ", "Time since start (ms): "), g_tick_ms);
    show_and_reset();
}
