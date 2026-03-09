/* ============================================================
 * NumWorks OS — Main Entry Point
 * Called by Reset_Handler after BSS/data init
 * Boot sequence: HAL → FS → USB → Kernel → Shell → Loop
 * ============================================================ */
#include "hal/hal.h"
#include "hal/display.h"
#include "hal/keyboard.h"
#include "hal/timer.h"
#include "kernel/kernel.h"
#include "fs/flashfs.h"
#include "shell/shell.h"
#include "ui/filemanager.h"
#include "usb/usb_cdc.h"
#include "include/config.h"
#include <string.h>

/* ── Boot splash screen ───────────────────────────────────── */
static void boot_splash(uint32_t ms_start) {
    display_fill(BLACK);

    /* Logo / title */
    display_str(60, 60,  "NumWorks OS",     WHITE,  BLACK);
    display_str(60, 76,  "v0.1 - nwos",     GREY,   BLACK);
    display_str(30, 100, "STM32F730 / 216MHz", GREY, BLACK);
    display_str(30, 116, "256KB RAM / 512KB Flash", GREY, BLACK);

    /* Progress bar outline */
    display_rect(40, 160, 240, 12, DKGREY);

    uint32_t elapsed;
    do {
        elapsed = hal_tick_ms() - ms_start;
        int progress = (int)(elapsed * 240 / 1200);
        if (progress > 240) progress = 240;
        display_fill_rect(41, 161, progress, 10, BLUE);
    } while (elapsed < 1200);
}

/* ── Check for TOOLBOX key at boot → file manager ─────────── */
static bool toolbox_held_at_boot(void) {
    return keyboard_is_pressed(KEY_TOOLBOX);
}

/* ── Main ─────────────────────────────────────────────────── */
int main(void) {
    uint32_t boot_start = 0;

    /* 1. Hardware init (clock, SysTick, GPIO, display, keyboard) */
    hal_init();
    display_init();
    keyboard_init();
    hal_timer_init();
    boot_start = hal_tick_ms();

    /* 2. Kernel init (events, memory pools) */
    kernel_init();

    /* 3. Filesystem init */
    int fserr = flashfs_init();
    if (fserr != 0) {
        /* Flash FS corrupt — display error and format */
        display_fill(BLACK);
        display_str(0, 0, "FS init failed -- formatting...", RED, BLACK);
        hal_delay_ms(800);
        flashfs_format();
    }

    /* 4. USB CDC init */
    usb_cdc_init();

    /* 5. File manager init */
    fm_init();

    /* 6. Boot splash */
    boot_splash(boot_start);

    /* 7. Create default welcome script if not present */
    {
        if (!flashfs_exists("/scripts/hello.py")) {
            const char *hello =
                "# Welcome to NumWorks OS!\n"
                "import display\n"
                "display.fill(display.BLACK)\n"
                "display.str(60, 100, 'Hello from Python!', display.WHITE, display.BLACK)\n"
                "display.flush()\n";
            flashfs_write("/scripts/hello.py", hello, (uint32_t)strlen(hello));
        }
    }

    /* 8. Choose startup mode */
    if (toolbox_held_at_boot()) {
        /* TOOLBOX held → launch file manager directly */
        kernel_set_app(APP_FILEMANAGER);
        fm_redraw();
    } else {
        /* Default → shell */
        shell_init();
    }

    /* 9. Kernel event loop — never returns */
    kernel_run();

    /* Should never reach here */
    return 0;
}

/* ── Newlib stubs (minimal libc support) ──────────────────── */
#include <sys/stat.h>
#include <errno.h>
#undef errno
extern int errno;
int errno;

int _close(int fd)          { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)         { (void)fd; return 1; }
int _lseek(int fd, int ptr, int dir) { (void)fd;(void)ptr;(void)dir; return 0; }
int _read(int fd, char *ptr, int len) { (void)fd;(void)ptr;(void)len; return 0; }
int _write(int fd, char *ptr, int len) {
    (void)fd;
    /* Route libc printf → shell */
    extern void shell_putc(char);
    for (int i = 0; i < len; i++) shell_putc(ptr[i]);
    return len;
}

/* Heap for newlib malloc (we use our own, but provide _sbrk stub) */
extern char _sheap, _eheap;
static char *heap_ptr = NULL;
void *_sbrk(int incr) {
    if (!heap_ptr) heap_ptr = &_sheap;
    char *prev = heap_ptr;
    if (heap_ptr + incr > &_eheap) {
        errno = ENOMEM;
        return (void*)-1;
    }
    heap_ptr += incr;
    return (void*)prev;
}

void _exit(int code) {
    (void)code;
    /* Reset the MCU */
    *((volatile uint32_t*)0xE000ED0C) = 0x05FA0004UL;
    while(1) {}
}

int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void)           { return 1; }
