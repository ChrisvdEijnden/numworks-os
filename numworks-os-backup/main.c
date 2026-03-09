/* ============================================================
 * NumWorks OS — Main Entry Point
 * Called by Reset_Handler after BSS/data init
 * Boot sequence: HAL → FS → USB → Kernel → Shell → Loop
 * ============================================================ */
#include "hal/hal.h"
#include "hal/display.h"
#include "hal/keyboard.h"
#include "kernel/kernel.h"
#include "fs/fs.h"
#include "shell/shell.h"
#include "ui/filemanager.h"
#include "usb/usb_cdc.h"
#include "include/config.h"
#include <string.h>


/* ── Boot splash screen ───────────────────────────────────── */
static void boot_splash(uint32_t ms_start) {
    display_fill(BLACK);

    /* Logo / title */
    display_str(60, 60,  "NumWorks OS",     CYAN, BLACK);
    display_str(60, 76,  "v0.1 - nwos",     GREY,   BLACK);
    display_str(30, 100, "STM32F730 / 192MHz", GREY, BLACK);
    display_str(30, 116, "256KB RAM / 512KB Flash", GREY, BLACK);

    /* Progress bar outline */
    display_rect(40, 160, 240, 12, DKGREY);

    uint32_t elapsed;
    do {
        elapsed = hal_tick_ms() - ms_start;
        int progress = (int)(elapsed * 240 / 1200);
        if (progress > 240) progress = 240;
        display_rect(40, 160, progress, 12, CYAN);
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
    boot_start = hal_tick_ms();

    /* 2. Kernel init (events, memory pools) */
    kernel_main();

    /* 3. Filesystem init */
    fs_err_t fserr = fs_init();
    if (fserr != FS_OK) {
        /* Flash FS corrupt — display error and format */
        display_fill(BLACK);
        display_str(0, 0, "FS init failed — formatting...", RED, BLACK);
        hal_delay_ms(800);
        fs_format();
        /* Create default directories */
        fs_mkdir("/scripts");
        fs_mkdir("/data");
    }

    /* 4. USB CDC init */
    usb_cdc_init();

    /* 5. File manager init */
    fm_init();

    /* 6. Boot splash */
    boot_splash(boot_start);

    /* 7. Create default welcome script if not present */
    {
        fs_stat_t st;
        if (fs_stat("/scripts/hello.py", &st) != FS_OK) {
            fs_file_t *f = fs_open("/scripts/hello.py",
                                   FS_O_WRONLY|FS_O_CREAT);
            if (f) {
                const char *hello =
                    "# Welcome to NumWorks OS!\n"
                    "import display\n"
                    "display.fill(display.BLACK)\n"
                    "display.text('Hello from Python!', 60, 100,\n"
                    "             display.GREEN, display.BLACK)\n";
                fs_write(f, hello, strlen(hello));
                fs_close(f);
            }
        }
    }

    /* 8. Choose startup mode */
    if (toolbox_held_at_boot()) {
        /* TOOLBOX held → launch file manager directly */
        fm_init(); fm_redraw();
    } else {
        /* Default → shell */
        shell_init();
    }

    /* 9. Kernel event loop — never returns */
    // kernel already running

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
extern char _heap_start, _heap_end;
static char *heap_ptr = NULL;
void *_sbrk(int incr) {
    if (!heap_ptr) heap_ptr = &_heap_start;
    char *prev = heap_ptr;
    if (heap_ptr + incr > &_heap_end) {
        errno = ENOMEM;
        return (void*)-1;
    }
    heap_ptr += incr;
    return (void*)prev;
}

void _exit(int code) {
    (void)code;
    /* Reset the MCU */
    extern void REG(uint32_t);
    *((volatile uint32_t*)0xE000ED0C) = 0x05FA0004UL;
    while(1) {}
}

int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void)           { return 1; }
