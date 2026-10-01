/* ================================================================
 * NumWorks OS — newlib system call stubs
 * File: hal/syscalls.c
 *
 * printf & co. write to the shell; malloc's heap is the RAM the linker
 * leaves after .bss (_sheap .. _eheap).
 * ================================================================ */
#include <errno.h>
#undef errno
extern int errno;
int errno;

#include <stdint.h>
#include <stddef.h>
#include <sys/stat.h>
#include "hal.h"

extern void shell_putc(char c);

int _close(int fd)                          { (void)fd; return -1; }
/* stdout is a character device, so newlib line-buffers it */
int _fstat(int fd, struct stat *st)         { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                         { (void)fd; return 1; }
int _lseek(int fd, int ptr, int dir)        { (void)fd;(void)ptr;(void)dir; return 0; }
int _read(int fd, char *ptr, int len)       { (void)fd;(void)ptr;(void)len; return 0; }
int _write(int fd, char *ptr, int len) {
    (void)fd;
    for (int i = 0; i < len; i++) shell_putc(ptr[i]);
    return len;
}

extern char _sheap, _eheap;
static char *heap_ptr = NULL;
void *_sbrk(int incr) {
    if (!heap_ptr) heap_ptr = &_sheap;
    char *prev = heap_ptr;
    if (incr > &_eheap - heap_ptr) { errno = ENOMEM; return (void*)-1; }
    heap_ptr += incr;
    return (void*)prev;
}

void hal_heap_stats(uint32_t *used, uint32_t *total) {
    if (used)  *used  = heap_ptr ? (uint32_t)(heap_ptr - &_sheap) : 0;
    if (total) *total = (uint32_t)(&_eheap - &_sheap);
}

void _exit(int code) {
    (void)code;
    hal_panic("programma gestopt (exit/abort)");
}
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void)           { return 1; }

/* assert() failures (builds without NDEBUG) show the crash screen */
void __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)func; (void)expr;
    static char msg[64];
    int n = 0;
    for (const char *p = "assert "; *p && n < 40; p++) msg[n++] = *p;
    const char *base = file;
    for (const char *p = file; *p; p++) if (*p == '/') base = p + 1;
    for (const char *p = base; *p && n < 52; p++) msg[n++] = *p;
    msg[n++] = ':';
    char digits[10];
    int d = 0;
    unsigned v = (unsigned)line;
    do { digits[d++] = (char)('0' + v % 10); v /= 10; } while (v && d < 10);
    while (d && n < 62) msg[n++] = digits[--d];
    msg[n] = 0;
    hal_panic(msg);
}
