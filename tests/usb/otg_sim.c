/* A model of the STM32 OTG_FS (DWC2) core in device mode, slave (no
 * DMA), as RM0431 chapter 32 describes it, plus a USB host driving it.
 * Strict where the manual allows a choice: after a SETUP both EP0
 * directions are NAKed and EP0 OUT is disabled, an OUT endpoint NAKs
 * after each transfer, an endpoint only takes data while enabled. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "otg_sim.h"

void usb_device_irq(void);

#define NEP 6
#define B(n) (1u << (n))
#define EPENA B(31)
#define EPDIS B(30)
#define SD0PID B(28)
#define SNAK B(27)
#define CNAK B(26)
#define STALL B(21)
#define NAKSTS B(17)
#define USBAEP B(15)

typedef struct { uint32_t ctl, intr, tsiz; uint8_t fifo[2048]; int flen; int nak; int toggle; } inep_t;
typedef struct { uint32_t ctl, intr, tsiz; int nak; int toggle; } outep_t;
typedef struct { uint32_t st; uint8_t data[80]; int len, pos; } rxent_t;

static inep_t  in[NEP];
static outep_t out[NEP];
static rxent_t rxq[64]; static int rxh, rxt;
static rxent_t cur;                          /* entry popped last, for DFIFO reads */
static uint32_t latched, gintmsk, gahbcfg, gusbcfg, gccfg, grxfsiz, txf0, txf[NEP], dcfg, dctl,
                diepmsk, doepmsk, daintmsk, pcgcctl, gotgint;
bool sim_vbus = true;
int sim_errors;
int sim_reg_writes;

static void err(const char *m) { printf("  SIM ERROR: %s\n", m); sim_errors++; }

static void core_reset(void) {
    memset(in, 0, sizeof in); memset(out, 0, sizeof out);
    for (int i = 0; i < NEP; i++) { in[i].nak = 1; out[i].nak = 1; }
    rxh = rxt = 0; latched = 0; gintmsk = 0; gahbcfg = 0; dcfg = 0; dctl = B(1); diepmsk = doepmsk = daintmsk = 0;
    gotgint = 0;
}

static void rx_push(uint32_t st, const uint8_t *d, int n) {
    rxent_t *e = &rxq[rxt]; rxt = (rxt + 1) % 64;
    if (rxt == rxh) err("rx fifo overflow");
    e->st = st; e->len = n; e->pos = 0; if (n) memcpy(e->data, d, n);
}
#define ST(ep, n, kind) ((uint32_t)(ep) | ((uint32_t)(n) << 4) | ((uint32_t)(kind) << 17))

static uint32_t daint(void) {
    uint32_t d = 0;
    for (int i = 0; i < NEP; i++) {
        if (in[i].intr & (diepmsk | B(1))) d |= B(i);
        if (out[i].intr & (doepmsk | B(1))) d |= B(16 + i);
    }
    return d;
}

static uint32_t gintsts(void) {
    uint32_t s = latched;
    if (rxh != rxt) s |= B(4);
    uint32_t d = daint() & daintmsk;
    if (d & 0xFFFF) s |= B(18);
    if (d >> 16) s |= B(19);
    if (gotgint) s |= B(2);
    return s;
}

uint32_t usbsim_rd(uint32_t off) {
    if (off >= 0x1000) {                     /* DFIFO: data of the popped entry */
        uint32_t w = 0;
        for (int k = 0; k < 4; k++) {
            if (cur.pos < cur.len) w |= (uint32_t)cur.data[cur.pos] << (8 * k);
            cur.pos++;
        }
        if (cur.pos > ((cur.len + 3) & ~3)) err("read past the received packet");
        return w;
    }
    if (off >= 0x900 && off < 0xB00) {
        int n = (off - 0x900) / 0x20, r = (off - 0x900) % 0x20;
        if (r == 0x00) return in[n].ctl | (in[n].nak ? NAKSTS : 0);
        if (r == 0x08) return in[n].intr;
        if (r == 0x10) return in[n].tsiz;
        if (r == 0x18) return 16;            /* DTXFSTS: plenty of room */
        return 0;
    }
    if (off >= 0xB00 && off < 0xD00) {
        int n = (off - 0xB00) / 0x20, r = (off - 0xB00) % 0x20;
        if (r == 0x00) return out[n].ctl | (out[n].nak ? NAKSTS : 0);
        if (r == 0x08) return out[n].intr;
        if (r == 0x10) return out[n].tsiz;
        return 0;
    }
    switch (off) {
    case 0x004: return gotgint;
    case 0x008: return gahbcfg;
    case 0x00C: return gusbcfg;
    case 0x010: return B(31);                /* AHBIDL; resets/flushes finish at once */
    case 0x014: return gintsts();
    case 0x018: return gintmsk;
    case 0x01C: return rxh != rxt ? rxq[rxh].st : 0;
    case 0x020: {
        if (rxh == rxt) { err("GRXSTSP read with an empty fifo"); return 0; }
        if (cur.pos < cur.len) err("previous packet not fully read");
        cur = rxq[rxh]; rxh = (rxh + 1) % 64;
        int ep = cur.st & 15, kind = (cur.st >> 17) & 15;
        if (kind == 4) out[0].intr |= B(3);              /* SETUP done -> STUP */
        if (kind == 3) out[ep].intr |= B(0);             /* OUT done -> XFRC */
        return cur.st;
    }
    case 0x024: return grxfsiz;
    case 0x028: return txf0;
    case 0x038: return gccfg;
    case 0x800: return dcfg;
    case 0x804: return dctl;
    case 0x810: return diepmsk;
    case 0x814: return doepmsk;
    case 0x818: return daint();
    case 0x81C: return daintmsk;
    case 0xE00: return pcgcctl;
    }
    if (off >= 0x104 && off < 0x104 + 4 * NEP) return txf[(off - 0x100) / 4];
    return 0;
}

static void write_ctl(uint32_t *ctl, int *nak, int *toggle, uint32_t *intr, uint32_t v, int ep) {
    uint32_t keep_ena = *ctl & EPENA;
    uint32_t stall = ep == 0 ? ((*ctl | v) & STALL) : (v & STALL);
    *ctl = (v & ~(EPENA | EPDIS | SNAK | CNAK | SD0PID | B(29) | STALL | NAKSTS)) | keep_ena | stall;
    if (v & SNAK) *nak = 1;
    if (v & CNAK) *nak = 0;
    if (v & SD0PID) *toggle = 0;
    if (v & EPENA) *ctl |= EPENA;
    if ((v & EPDIS) && (*ctl & EPENA)) { *ctl &= ~EPENA; *intr |= B(1); }
}

void usbsim_wr(uint32_t off, uint32_t v) {
    sim_reg_writes++;
    if (off >= 0x1000) {
        int ep = (off - 0x1000) / 0x1000;
        if (ep >= NEP) { err("fifo write past the endpoints"); return; }
        if (in[ep].flen + 4 > (int)sizeof in[ep].fifo) { err("tx fifo overflow"); return; }
        memcpy(in[ep].fifo + in[ep].flen, &v, 4); in[ep].flen += 4;
        return;
    }
    if (off >= 0x900 && off < 0xB00) {
        int n = (off - 0x900) / 0x20, r = (off - 0x900) % 0x20;
        if (r == 0x00) write_ctl(&in[n].ctl, &in[n].nak, &in[n].toggle, &in[n].intr, v, n);
        else if (r == 0x08) in[n].intr &= ~v;
        else if (r == 0x10) in[n].tsiz = v;
        return;
    }
    if (off >= 0xB00 && off < 0xD00) {
        int n = (off - 0xB00) / 0x20, r = (off - 0xB00) % 0x20;
        if (r == 0x00) write_ctl(&out[n].ctl, &out[n].nak, &out[n].toggle, &out[n].intr, v, n);
        else if (r == 0x08) out[n].intr &= ~v;
        else if (r == 0x10) out[n].tsiz = v;
        return;
    }
    switch (off) {
    case 0x004: gotgint &= ~v; return;
    case 0x008: gahbcfg = v; return;
    case 0x00C: gusbcfg = v; return;
    case 0x010:
        if (v & B(0)) core_reset();
        if (v & B(5)) {
            int f = (v >> 6) & 31;
            for (int i = 0; i < NEP; i++) if (f == 0x10 || f == i) in[i].flen = 0;
        }
        if (v & B(4)) rxh = rxt = 0;
        return;
    case 0x014: latched &= ~v; return;
    case 0x018: gintmsk = v; return;
    case 0x024: grxfsiz = v; return;
    case 0x028: txf0 = v; return;
    case 0x038: gccfg = v; return;
    case 0x800: dcfg = v; return;
    case 0x804: dctl = v & ~(B(7) | B(8) | B(9) | B(10)); return;
    case 0x810: diepmsk = v; return;
    case 0x814: doepmsk = v; return;
    case 0x81C: daintmsk = v; return;
    case 0xE00: pcgcctl = v; return;
    }
    if (off >= 0x104 && off < 0x104 + 4 * NEP) { txf[(off - 0x100) / 4] = v; return; }
}

bool usbsim_vbus(void) { return sim_vbus; }

/* ── Host side ─────────────────────────────────────────────────── */
static int host_addr;
static bool addr_check = true;

static void run_irq(void) {
    for (int i = 0; i < 100; i++) {
        if (!(gahbcfg & 1) || !(gintsts() & gintmsk)) return;
        usb_device_irq();
    }
    err("interrupt never cleared");
}

static bool addressed(void) {
    if (dctl & B(1)) { err("host talked to a soft-disconnected device"); return false; }
    if (addr_check && (int)((dcfg >> 4) & 0x7F) != host_addr) return false;
    return true;
}

void host_bus_reset(void) {
    host_addr = 0;
    latched |= B(12); run_irq();
    latched |= B(13); run_irq();
}

void host_set_address(int a) { host_addr = a; }
void host_addr_check(bool on) { addr_check = on; }
bool host_connected(void) { return !(dctl & B(1)); }

static int mps_in(int ep) {
    if (ep == 0) { static const int m[4] = {64, 32, 16, 8}; return m[in[0].ctl & 3]; }
    return in[ep].ctl & 0x7FF;
}

int host_setup(const uint8_t s[8]) {
    if (!addressed()) return HOST_TIMEOUT;
    in[0].ctl &= ~STALL; out[0].ctl &= ~STALL;
    in[0].nak = 1; out[0].nak = 1; out[0].ctl &= ~EPENA;
    in[0].toggle = 1; out[0].toggle = 1;          /* data stage starts with DATA1 */
    rx_push(ST(0, 8, 6), s, 8);
    rx_push(ST(0, 0, 4), NULL, 0);
    run_irq();
    return 0;
}

int host_in(int ep, uint8_t *buf) {
    if (!addressed()) return HOST_TIMEOUT;
    inep_t *e = &in[ep];
    if (e->ctl & STALL) return HOST_STALL;
    if (ep && !(e->ctl & USBAEP)) return HOST_TIMEOUT;
    if (!(e->ctl & EPENA) || e->nak) return HOST_NAK;
    int pktmask = ep == 0 ? 3 : 0x3FF, sizemask = ep == 0 ? 0x7F : 0x7FFFF;
    int size = e->tsiz & sizemask, pkts = (e->tsiz >> 19) & pktmask;
    if (pkts == 0) { err("IN endpoint enabled with PKTCNT 0"); return HOST_NAK; }
    int n = size < mps_in(ep) ? size : mps_in(ep);
    int words = (n + 3) / 4;
    if (e->flen < words * 4) return HOST_NAK;      /* data not in the fifo yet */
    memcpy(buf, e->fifo, n);
    memmove(e->fifo, e->fifo + words * 4, e->flen - words * 4); e->flen -= words * 4;
    size -= n; pkts--;
    e->tsiz = (e->tsiz & ~(sizemask | ((uint32_t)pktmask << 19))) | size | ((uint32_t)pkts << 19);
    if (ep) e->toggle ^= 1;
    if (pkts == 0) {
        e->ctl &= ~EPENA;
        e->intr |= B(0);
        if (e->flen) err("data left in the tx fifo after the transfer");
    }
    run_irq();
    return n;
}

int host_out(int ep, const uint8_t *buf, int n) {
    if (!addressed()) return HOST_TIMEOUT;
    outep_t *e = &out[ep];
    if (e->ctl & STALL) return HOST_STALL;
    if (ep && !(e->ctl & USBAEP)) return HOST_TIMEOUT;
    if (!(e->ctl & EPENA) || e->nak) return HOST_NAK;
    int pktmask = ep == 0 ? 1 : 0x3FF, sizemask = ep == 0 ? 0x7F : 0x7FFFF;
    int size = e->tsiz & sizemask, pkts = (e->tsiz >> 19) & pktmask;
    if (pkts == 0) { err("OUT endpoint enabled with PKTCNT 0"); return HOST_NAK; }
    if (n > size) { err("OUT packet larger than the transfer size"); return HOST_NAK; }
    rx_push(ST(ep, n, 2), buf, n);
    size -= n; pkts--;
    e->tsiz = (e->tsiz & ~(sizemask | ((uint32_t)pktmask << 19))) | size | ((uint32_t)pkts << 19);
    if (ep) e->toggle ^= 1;
    if (pkts == 0 || n < (ep == 0 ? 64 : (int)(e->ctl & 0x7FF))) {
        e->ctl &= ~EPENA; e->nak = 1;
        rx_push(ST(ep, 0, 3), NULL, 0);
    }
    run_irq();
    return n;
}

int sim_in_toggle(int ep) { return in[ep].toggle; }
int sim_out_toggle(int ep) { return out[ep].toggle; }
uint32_t sim_dcfg(void) { return dcfg; }
void sim_session_end(void) { gotgint |= B(2); run_irq(); }
