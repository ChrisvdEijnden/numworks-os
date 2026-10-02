/* End-to-end test of usb/usb_device.c on the simulated OTG core: a host
 * enumerates the calculator the way Linux does, checks the descriptors,
 * then runs the PC transfer protocol (usb/usb_cdc.c, real flashfs on
 * simulated flash) through the bulk endpoints. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "otg_sim.h"

void usb_device_init(void);
void usb_device_poll(void);
bool usb_device_configured(void);
void usb_cdc_process(void);
int flashfs_init(void); int flashfs_format(void);
int flashfs_write(const char *path, const void *data, uint32_t len);
bool flashfs_map(const char *path, const char **data, uint32_t *size);

static uint32_t now;
uint32_t hal_tick_ms(void) { return now; }

static int fails;
static void check(int c, const char *w) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }

static void pump(void) { now++; usb_cdc_process(); }

/* ── Control transfers ─────────────────────────────────────────── */
static int ctrl(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t length,
                const uint8_t *out_data, uint8_t *in_data) {
    uint8_t s[8] = { type, req, value & 0xFF, value >> 8, index & 0xFF, index >> 8, length & 0xFF, length >> 8 };
    if (host_setup(s) < 0) return HOST_TIMEOUT;
    int got = 0;
    if (type & 0x80) {                                  /* IN data stage */
        uint8_t pkt[64];
        for (int tries = 0; tries < 1000; ) {
            int n = host_in(0, pkt);
            if (n == HOST_NAK) { pump(); tries++; continue; }
            if (n < 0) return n;
            if (got + n > length) { printf("  device sent more than wLength\n"); return HOST_TIMEOUT; }
            memcpy(in_data + got, pkt, n); got += n;
            if (n < 64 || got == length) break;
        }
        for (int tries = 0; tries < 1000; tries++) {    /* status: OUT ZLP */
            int r = host_out(0, NULL, 0);
            if (r == HOST_NAK) { pump(); continue; }
            if (r < 0) return r;
            return got;
        }
        return HOST_TIMEOUT;
    }
    if (length) {                                       /* OUT data stage */
        for (int tries = 0; ; tries++) {
            int r = host_out(0, out_data, length);
            if (r == HOST_NAK && tries < 1000) { pump(); continue; }
            if (r < 0) return r;
            break;
        }
    }
    uint8_t pkt[64];
    for (int tries = 0; tries < 1000; tries++) {        /* status: IN ZLP */
        int n = host_in(0, pkt);
        if (n == HOST_NAK) { pump(); continue; }
        if (n < 0) return n;
        if (n != 0) { printf("  status stage carried data\n"); return HOST_TIMEOUT; }
        return 0;
    }
    return HOST_TIMEOUT;
}

static int get_desc(uint8_t type, uint8_t idx, uint16_t len, uint8_t *buf) {
    return ctrl(0x80, 6, (uint16_t)(type << 8 | idx), type == 3 && idx ? 0x0409 : 0, len, NULL, buf);
}

/* ── Bulk ──────────────────────────────────────────────────────── */
static int out_naks;
static void bulk_write(const void *data, int len) {
    const uint8_t *p = data;
    int last = -1;
    while (len > 0 || last == 64) {
        int n = len > 64 ? 64 : len;
        int r = host_out(1, p, n);
        if (r == HOST_NAK) { out_naks++; pump(); continue; }
        if (r < 0) { printf("  bulk OUT error %d\n", r); fails++; return; }
        p += n; len -= n; last = n;
        if (len == 0) break;
    }
}

/* Like a Linux URB: done on a short packet (a ZLP too) or when full.
 * Returns -1 if the device never finished it. */
static int bulk_read_urb(uint8_t *buf, int max) {
    int got = 0, idle = 0;
    uint8_t pkt[64];
    while (got < max) {
        int n = host_in(1, pkt);
        if (n == HOST_NAK) { if (++idle > 3000) return got ? -1 : 0; pump(); continue; }
        if (n < 0) return -1;
        idle = 0;
        memcpy(buf + got, pkt, n); got += n;
        if (n < 64) break;
    }
    return got;
}

static char rbuf[20000]; static int rlen;
static int urb_hangs;
/* Read until `want` appears (or nothing more comes) */
static bool read_until(const char *want) {
    for (int i = 0; i < 400; i++) {
        uint8_t u[128];
        int n = bulk_read_urb(u, 128);
        if (n < 0) { urb_hangs++; return false; }
        memcpy(rbuf + rlen, u, n); rlen += n; rbuf[rlen] = 0;
        if (memmem(rbuf, rlen, want, strlen(want))) return true;
        if (n == 0) pump();
    }
    return false;
}

static uint8_t big[9000];

int main(void) {
    flashfs_init();
    flashfs_format();

    printf("Connection and enumeration\n");
    sim_vbus = false;
    usb_device_init();
    pump();
    check(!host_connected(), "no VBUS: soft-disconnected (no D+ pull-up)");
    sim_vbus = true; pump();
    check(host_connected(), "VBUS: connects");

    host_bus_reset();
    uint8_t d[256];
    int n = get_desc(1, 0, 64, d);                      /* Linux asks for 64 first, at address 0 */
    check(n == 18 && d[0] == 18 && d[1] == 1, "device descriptor at address 0 (asked 64, got 18)");
    check(d[7] == 64 && d[4] == 2 && (d[8] | d[9] << 8) == 0x1209, "EP0 64 bytes, class CDC, VID 0x1209");
    host_bus_reset();
    host_addr_check(false);
    n = ctrl(0x00, 5, 7, 0, 0, NULL, NULL);
    host_set_address(7);
    host_addr_check(true);
    check(n == 0 && ((sim_dcfg() >> 4) & 0x7F) == 7, "SET_ADDRESS 7");
    n = get_desc(1, 0, 18, d);
    check(n == 18, "device descriptor at the new address");
    n = get_desc(2, 0, 9, d);
    int total = d[2] | d[3] << 8;
    check(n == 9 && total == 67, "configuration header: 67 bytes in all");
    uint8_t cfg[256];
    n = get_desc(2, 0, 255, cfg);
    check(n == 67, "whole configuration (asked 255): 67 bytes, a short last packet ends it");

    /* Walk the descriptors like the kernel's parser */
    int ifaces = 0, eps = 0, ok = 1, cdc_func = 0;
    for (int i = 0; i < n; ) {
        int len = cfg[i], type = cfg[i + 1];
        if (len < 2 || i + len > n) { ok = 0; break; }
        if (type == 4) { ifaces++; if (cfg[i + 5] == 2 && cfg[i + 6] != 2) ok = 0; }
        if (type == 5) { eps++; if (len != 7) ok = 0; }
        if (type == 0x24) cdc_func++;
        i += len;
    }
    check(ok && ifaces == 2 && eps == 3 && cdc_func == 4 && cfg[4] == 2, "2 interfaces (comm ACM + data), 3 endpoints, 4 CDC functional descriptors");
    n = get_desc(3, 0, 255, d);
    check(n == 4 && d[2] == 0x09 && d[3] == 0x04, "string 0: English (US)");
    n = get_desc(3, 2, 255, d);
    char prod[64] = {0}; for (int i = 2; i < n; i += 2) prod[(i - 2) / 2] = d[i];
    check(n > 2 && strcmp(prod, "NumWorks OS file transfer") == 0, "product string");
    n = get_desc(3, 3, 255, d);
    check(n == 50, "serial number: 24 hex digits from the chip ID");
    n = get_desc(3, 9, 255, d);
    check(n == HOST_STALL, "a string that doesn't exist: STALL");
    n = get_desc(6, 0, 10, d);
    check(n == HOST_STALL, "device qualifier (high speed): STALL");
    n = get_desc(1, 0, 18, d);
    check(n == 18, "the next request works after a STALL");
    n = get_desc(2, 0, 64, cfg);
    check(n == 64, "configuration asked with wLength 64: exactly 64, no ZLP needed");
    n = get_desc(1, 0, 8, d);
    check(n == 8, "device descriptor cut to wLength 8");

    check(!usb_device_configured(), "not configured yet");
    n = ctrl(0x00, 9, 1, 0, 0, NULL, NULL);
    check(n == 0 && usb_device_configured(), "SET_CONFIGURATION 1");
    n = ctrl(0x80, 8, 0, 0, 1, NULL, d);
    check(n == 1 && d[0] == 1, "GET_CONFIGURATION: 1");
    n = ctrl(0x00, 9, 2, 0, 0, NULL, NULL);
    check(n == HOST_STALL, "SET_CONFIGURATION 2: STALL");
    n = ctrl(0x00, 9, 1, 0, 0, NULL, NULL);
    check(n == 0 && usb_device_configured(), "and configured again");
    const uint8_t lc[7] = { 0x00, 0x96, 0x00, 0x00, 0, 0, 8 };    /* 38400 8N1 */
    n = ctrl(0x21, 0x20, 0, 0, 7, lc, NULL);
    check(n == 0, "SET_LINE_CODING (OUT data stage)");
    n = ctrl(0xA1, 0x21, 0, 0, 7, NULL, d);
    check(n == 7 && memcmp(d, lc, 7) == 0, "GET_LINE_CODING returns it");
    n = ctrl(0x21, 0x22, 3, 0, 0, NULL, NULL);
    check(n == 0, "SET_CONTROL_LINE_STATE (DTR, RTS)");
    n = ctrl(0x21, 0x99, 0, 0, 0, NULL, NULL);
    check(n == HOST_STALL, "unknown class request: STALL");
    n = ctrl(0x82, 0, 0, 0x81, 2, NULL, d);
    check(n == 2 && d[0] == 0, "GET_STATUS endpoint 0x81: not halted");

    printf("PC transfer over the bulk endpoints\n");
    const char *ls = "LIST\n";
    bulk_write(ls, 5);
    check(read_until("OK\r\n"), "LIST answered");

    for (int i = 0; i < (int)sizeof big; i++) big[i] = (uint8_t)(i * 7 + (i >> 8));
    char cmd[64];
    int sz = 8000;
    snprintf(cmd, sizeof cmd, "SEND big.bin %d\n", sz);
    rlen = 0;
    bulk_write(cmd, (int)strlen(cmd));
    check(read_until("READY\r\n"), "SEND big.bin 8000: READY");
    out_naks = 0;
    rlen = 0;
    bulk_write(big, sz);                                /* faster than the device drains */
    check(read_until("OK\r\n"), "8000 bytes sent: OK");
    printf("       the device NAKed %d times while its buffer was full\n", out_naks);
    const char *fd; uint32_t fsz;
    check(flashfs_map("big.bin", &fd, &fsz) && fsz == (uint32_t)sz && memcmp(fd, big, sz) == 0,
          "file on flash is identical (nothing dropped)");
    check(out_naks > 0, "flow control was exercised");

    rlen = 0;
    bulk_write("RECV big.bin\n", 13);
    bool got = read_until("OK\r\n");
    char *data = memmem(rbuf, rlen, "\r\n", 2);
    check(got && strncmp(rbuf, "DATA 8000\r\n", 11) == 0 && data && memcmp(data + 2, big, sz) == 0 &&
          rlen == 11 + sz + 4, "RECV big.bin: DATA 8000, the same bytes, OK");

    /* Responses that end exactly on a packet boundary need a ZLP,
     * or a host reading 128-byte URBs waits for ever */
    static const int sizes[] = { 51, 114, 178, 242, 306 };   /* header + n + 4 = 64, 128, 192, 256, 320 */
    int boundary_ok = 0;
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        flashfs_write("z.bin", big, (uint32_t)sizes[k]);
        rlen = 0; urb_hangs = 0;
        snprintf(cmd, sizeof cmd, "RECV z.bin\n");
        bulk_write(cmd, (int)strlen(cmd));
        bool ok2 = read_until("OK\r\n");
        printf("       %d bytes: ok=%d hangs=%d rlen=%d\n", sizes[k], ok2, urb_hangs, rlen);
        if (ok2 && urb_hangs == 0 && rlen == (sizes[k] < 100 ? 13 : 14) + sizes[k] && (rlen % 64) == 0) boundary_ok++;
    }
    check(boundary_ok == 5, "responses of 64/128/192/256/320 bytes all arrive (ZLP sent)");

    rlen = 0;
    bulk_write("DEL big.bin\n", 12);
    check(read_until("OK\r\n"), "DEL big.bin: OK");
    rlen = 0;
    bulk_write("FOO\n", 4);
    check(read_until("ERR"), "unknown command: ERR");

    int t_in = sim_in_toggle(1), t_out = sim_out_toggle(1);
    n = ctrl(0x02, 1, 0, 0x81, 0, NULL, NULL);
    check(n == 0 && sim_in_toggle(1) == 0, "CLEAR_FEATURE(halt) on 0x81 resets its data toggle");
    (void)t_in; (void)t_out;
    n = ctrl(0x02, 3, 0, 0x81, 0, NULL, NULL);
    n = host_in(1, d);
    check(n == HOST_STALL, "SET_FEATURE(halt) on 0x81: the endpoint STALLs");
    n = ctrl(0x82, 0, 0, 0x81, 2, NULL, d);
    check(n == 2 && d[0] == 1, "GET_STATUS 0x81: halted");
    n = ctrl(0x02, 1, 0, 0x81, 0, NULL, NULL);
    rlen = 0;
    bulk_write("LIST\n", 5);
    check(read_until("OK\r\n"), "after CLEAR_FEATURE the port works again");

    printf("Reset, unplug, replug\n");
    host_bus_reset();
    check(!usb_device_configured() && ((sim_dcfg() >> 4) & 0x7F) == 0, "bus reset: address 0, not configured");
    host_addr_check(false);
    ctrl(0x00, 5, 3, 0, 0, NULL, NULL);
    host_set_address(3); host_addr_check(true);
    n = ctrl(0x00, 9, 1, 0, 0, NULL, NULL);
    rlen = 0;
    bulk_write("LIST\n", 5);
    check(n == 0 && read_until("OK\r\n"), "enumerated again after the reset, LIST works");
    sim_vbus = false; sim_session_end(); pump();
    check(!host_connected() && !usb_device_configured(), "VBUS gone: disconnected, deconfigured");
    sim_vbus = true; pump();
    check(host_connected(), "VBUS back: connects again");

    printf("%s (%d failure%s, %d simulator error%s)\n", fails || sim_errors ? "FAILED" : "ALL PASSED",
           fails, fails == 1 ? "" : "s", sim_errors, sim_errors == 1 ? "" : "s");
    return fails || sim_errors;
}
