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

#include "hal.h"
#include "../include/stdint.h"
#include "../include/stddef.h"

struct stat;
extern void shell_putc(char c);

int _close(int fd)                          { (void)fd; return -1; }
/* Our include/sys/stat.h doesn't match newlib's struct stat, so don't
 * write through it: report "unknown" and newlib falls back to plain
 * buffering. */
int _fstat(int fd, struct stat *st)         { (void)fd; (void)st; errno = EINVAL; return -1; }
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
