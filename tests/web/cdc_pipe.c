/* stdin/stdout <-> usb_cdc.c ring buffers; real flashfs on simulated flash */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
int flashfs_init(void); int flashfs_format(void);
void usb_cdc_process(void); int usb_cdc_rx_push(const void *, int); int usb_cdc_tx_pop(void *, int);
void usb_device_init(void) {} void usb_device_poll(void) {}
uint32_t hal_tick_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
int main(void) {
    flashfs_init(); flashfs_format();
    fcntl(0, F_SETFL, O_NONBLOCK);
    uint8_t in[512], out[512]; int pending = 0, off = 0;
    for (;;) {
        if (pending == 0) { int n = (int)read(0, in, sizeof in); if (n == 0) return 0; if (n > 0) { pending = n; off = 0; } }
        if (pending) { int t = usb_cdc_rx_push(in + off, pending); off += t; pending -= t; }
        usb_cdc_process();
        int n = usb_cdc_tx_pop(out, sizeof out);
        if (n > 0) { fwrite(out, 1, (size_t)n, stdout); fflush(stdout); }
        usleep(100);
    }
}
