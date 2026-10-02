/* Host bridge: pty <-> usb_cdc.c ring buffers, real flashfs on simulated NOR flash */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
int flashfs_init(void); int flashfs_format(void);
void usb_cdc_process(void); int usb_cdc_rx_push(const void *, int); int usb_cdc_tx_pop(void *, int);
void usb_device_init(void) {} void usb_device_poll(void) {}   /* the bridge moves the bytes itself */
uint32_t hal_tick_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
int main(void) {
    flashfs_init();
    if (flashfs_format() != 0) { fprintf(stderr, "format failed\n"); return 1; }
    int m = posix_openpt(O_RDWR | O_NOCTTY); grantpt(m); unlockpt(m);
    printf("%s\n", ptsname(m)); fflush(stdout);
    fcntl(m, F_SETFL, O_NONBLOCK);
    uint8_t in[256], out[256]; int pending = 0, off = 0;
    for (;;) {
        if (pending == 0) { int n = read(m, in, sizeof in); if (n > 0) { pending = n; off = 0; } }
        if (pending) { int t = usb_cdc_rx_push(in + off, pending); off += t; pending -= t; }
        usb_cdc_process();
        int n = usb_cdc_tx_pop(out, sizeof out);
        if (n > 0) { int w = 0; while (w < n) { int r = write(m, out + w, n - w); if (r > 0) w += r; else usleep(100); } }
        usleep(200);
    }
}
