/* ================================================================
 * NumWorks OS — USB device stack (CDC-ACM serial port)
 * File: usb/usb_device.c
 *
 * The STM32F730's OTG_FS core (a Synopsys DWC2) in device mode, full
 * speed, without DMA (RM0431 chapter 32). The calculator shows up as a
 * serial port: a CDC-ACM function with
 *   EP0       control
 *   EP1 OUT   bulk, 64 bytes: data from the PC  -> usb_cdc_rx_push()
 *   EP1 IN    bulk, 64 bytes: data to the PC    <- usb_cdc_tx_pop()
 *   EP2 IN    interrupt: CDC notifications (declared, never sent)
 *
 * Pins: D- PA11, D+ PA12 (AF10); VBUS is sensed on PA9 (set up by
 * hal/battery.c). The core is soft-disconnected (no D+ pull-up) until
 * VBUS is present.
 *
 * Interrupts do the protocol work: SETUP packets are read from the
 * receive FIFO and handled when the core reports the SETUP stage done
 * (DOEPINT0.STUP). Bulk OUT packets go straight into the receive ring;
 * EP1 OUT is only re-armed while the ring has room for a full packet,
 * so the host is NAKed instead of data being dropped. A transfer to
 * the PC that ends on a full packet is closed with a zero-length
 * packet, or the host would wait for more.
 *
 * VID/PID: 0x1209 is pid.codes' open-source vendor ID; 0x0001 is its
 * test PID, fine for development. Request a PID of our own from
 * pid.codes before distributing builds. (NumWorks' and ST's IDs are
 * not ours to use.)
 * ================================================================ */
#include "usb_device.h"
#include "usb_cdc.h"
#include <string.h>

#ifdef USB_SIM
/* Host test build: registers are a simulated core */
uint32_t usbsim_rd(uint32_t off);
void     usbsim_wr(uint32_t off, uint32_t v);
bool     usbsim_vbus(void);
#define RD(off)     usbsim_rd(off)
#define WR(off, v)  usbsim_wr((off), (v))
#define VBUS()      usbsim_vbus()
#define IRQ_OFF()   (0U)
#define IRQ_ON(s)   ((void)(s))
#else
#include "../include/stm32f730.h"
#include "../include/config.h"
#include "../hal/hal.h"
#include "../hal/battery.h"
#define OTG_BASE    0x50000000UL
#define RD(off)     (*(volatile uint32_t *)(OTG_BASE + (off)))
#define WR(off, v)  (*(volatile uint32_t *)(OTG_BASE + (off)) = (v))
#define VBUS()      battery_usb_powered()
static inline uint32_t irq_off(void) {
    uint32_t s; __asm volatile("mrs %0, primask\n cpsid i" : "=r"(s) :: "memory"); return s;
}
#define IRQ_OFF()   irq_off()
#define IRQ_ON(s)   __asm volatile("msr primask, %0" :: "r"(s) : "memory")
#endif

#define SETB(off, b) WR(off, RD(off) | (b))
#define CLRB(off, b) WR(off, RD(off) & ~(b))

/* ── Registers (RM0431 §32.15) ─────────────────────────────────── */
#define GOTGINT   0x004U
#define GAHBCFG   0x008U
#define GUSBCFG   0x00CU
#define GRSTCTL   0x010U
#define GINTSTS   0x014U
#define GINTMSK   0x018U
#define GRXSTSP   0x020U
#define GRXFSIZ   0x024U
#define DIEPTXF0  0x028U
#define GCCFG     0x038U
#define DIEPTXF(n) (0x100U + 4U * (n))     /* n >= 1 */
#define DCFG      0x800U
#define DCTL      0x804U
#define DIEPMSK   0x810U
#define DOEPMSK   0x814U
#define DAINT     0x818U
#define DAINTMSK  0x81CU
#define DIEPCTL(n)  (0x900U + 0x20U * (n))
#define DIEPINT(n)  (0x908U + 0x20U * (n))
#define DIEPTSIZ(n) (0x910U + 0x20U * (n))
#define DOEPCTL(n)  (0xB00U + 0x20U * (n))
#define DOEPINT(n)  (0xB08U + 0x20U * (n))
#define DOEPTSIZ(n) (0xB10U + 0x20U * (n))
#define PCGCCTL   0xE00U
#define DFIFO(n)  (0x1000U + 0x1000U * (n))

#define GAHBCFG_GINTMSK  (1U << 0)
#define GUSBCFG_TRDT(x)  ((uint32_t)(x) << 10)
#define GUSBCFG_TRDT_MASK (0xFU << 10)
#define GUSBCFG_FHMOD    (1U << 29)
#define GUSBCFG_FDMOD    (1U << 30)
#define GRSTCTL_CSRST    (1U << 0)
#define GRSTCTL_RXFFLSH  (1U << 4)
#define GRSTCTL_TXFFLSH  (1U << 5)
#define GRSTCTL_TXFNUM(n) ((uint32_t)(n) << 6)
#define GRSTCTL_AHBIDL   (1U << 31)
#define GINT_OTGINT      (1U << 2)
#define GINT_RXFLVL      (1U << 4)
#define GINT_USBSUSP     (1U << 11)
#define GINT_USBRST      (1U << 12)
#define GINT_ENUMDNE     (1U << 13)
#define GINT_IEPINT      (1U << 18)
#define GINT_OEPINT      (1U << 19)
#define GINT_WKUPINT     (1U << 31)
#define GOTGINT_SEDET    (1U << 2)
#define GCCFG_PWRDWN     (1U << 16)
#define GCCFG_VBDEN      (1U << 21)
#define DCFG_DSPD_FS     (3U << 0)
#define DCFG_DAD_MASK    (0x7FU << 4)
#define DCTL_RWUSIG      (1U << 0)
#define DCTL_SDIS        (1U << 1)
#define DCTL_CGINAK      (1U << 8)
#define EPCTL_USBAEP     (1U << 15)
#define EPCTL_EPTYP(t)   ((uint32_t)(t) << 18)
#define EPCTL_STALL      (1U << 21)
#define EPCTL_TXFNUM(n)  ((uint32_t)(n) << 22)
#define EPCTL_CNAK       (1U << 26)
#define EPCTL_SNAK       (1U << 27)
#define EPCTL_SD0PID     (1U << 28)
#define EPCTL_EPDIS      (1U << 30)
#define EPCTL_EPENA      (1U << 31)
#define EPINT_XFRC       (1U << 0)
#define EPINT_EPDISD     (1U << 1)
#define EPINT_STUP       (1U << 3)
#define TSIZ_PKTCNT1     (1U << 19)
#define TSIZ_STUPCNT3    (3U << 29)
#define EPTYP_BULK       2U
#define EPTYP_INTR       3U

/* RX status (GRXSTSP), device mode */
#define PKTSTS_OUT_DATA     2U
#define PKTSTS_SETUP_DATA   6U

#define MPS 64U                 /* max packet size, every endpoint we use */
#define EP_DATA   1U            /* bulk IN 0x81 / OUT 0x01 */
#define EP_NOTIFY 2U            /* interrupt IN 0x82 */
#define NOTIFY_MPS 16U

/* ── Descriptors ───────────────────────────────────────────────── */
#define VID 0x1209U
#define PID 0x0001U

static const uint8_t DEVICE_DESC[18] = {
    18, 1,                      /* bLength, DEVICE */
    0x00, 0x02,                 /* USB 2.0 */
    0x02, 0x00, 0x00,           /* class CDC (interfaces say the rest) */
    MPS,                        /* EP0 max packet */
    VID & 0xFF, VID >> 8, PID & 0xFF, PID >> 8,
    0x00, 0x01,                 /* bcdDevice 1.00 */
    1, 2, 3,                    /* manufacturer, product, serial strings */
    1                           /* one configuration */
};

#define CONFIG_LEN 67
static const uint8_t CONFIG_DESC[CONFIG_LEN] = {
    9, 2, CONFIG_LEN, 0, 2, 1, 0, 0x80, 250,   /* 2 interfaces, bus powered, 500 mA (charging) */
    /* Interface 0: CDC communication, ACM, no protocol */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x00, 0,
    5, 0x24, 0x00, 0x10, 0x01,                 /* header, CDC 1.10 */
    5, 0x24, 0x01, 0x00, 1,                    /* call management: none, data interface 1 */
    4, 0x24, 0x02, 0x02,                       /* ACM: line coding + control line state */
    5, 0x24, 0x06, 0, 1,                       /* union: master 0, slave 1 */
    7, 5, 0x80 | EP_NOTIFY, 0x03, NOTIFY_MPS, 0, 16,   /* interrupt IN, 16 ms */
    /* Interface 1: CDC data */
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    7, 5, EP_DATA,        0x02, MPS, 0, 0,     /* bulk OUT */
    7, 5, 0x80 | EP_DATA, 0x02, MPS, 0, 0,     /* bulk IN */
};

static const char *const STRINGS[] = {
    NULL,                       /* 0: language list */
    "NumWorks OS project",
    "NumWorks OS file transfer",
    NULL,                       /* 3: serial number, from the chip's unique ID */
};

/* ── State ─────────────────────────────────────────────────────── */
static uint8_t  s_setup[8];
static uint8_t  s_ctrl_buf[64];          /* built descriptors, OUT data stage */
static const uint8_t *s_ep0_tx;
static uint16_t s_ep0_left;
static bool     s_ep0_zlp;               /* end the IN data stage with a ZLP */
static uint8_t  s_ep0_out_req;           /* class request waiting for its data stage */
static uint16_t s_ep0_out_len;
static uint8_t  s_config;
static uint8_t  s_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };   /* 115200 8N1 */
static bool     s_dtr;
static volatile bool s_tx_busy;          /* EP1 IN packet in flight */
static bool     s_tx_zlp;                /* last packet was full: may need a ZLP */
static volatile bool s_rx_armed;         /* EP1 OUT accepts a packet */
static bool     s_connected;
static bool     s_ready;                 /* usb_device_init() succeeded */

/* ── FIFO access ───────────────────────────────────────────────── */
static void fifo_write(unsigned ep, const uint8_t *p, unsigned n) {
    for (unsigned i = 0; i < n; i += 4) {
        uint32_t w = 0;
        for (unsigned k = 0; k < 4 && i + k < n; k++) w |= (uint32_t)p[i + k] << (8 * k);
        WR(DFIFO(ep), w);
    }
}

static void fifo_read(uint8_t *p, unsigned n) {
    for (unsigned i = 0; i < n; i += 4) {
        uint32_t w = RD(DFIFO(0));
        for (unsigned k = 0; k < 4 && i + k < n; k++) p[i + k] = (uint8_t)(w >> (8 * k));
    }
}

static void fifo_discard(unsigned n) {
    for (unsigned i = 0; i < n; i += 4) (void)RD(DFIFO(0));
}

static bool wait_clear(uint32_t off, uint32_t bits) {
    for (uint32_t n = 0; n < 200000U; n++) if (!(RD(off) & bits)) return true;
    return false;
}

static void flush_tx(unsigned fifo) {          /* 0x10: all */
    WR(GRSTCTL, GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM(fifo));
    wait_clear(GRSTCTL, GRSTCTL_TXFFLSH);
}

/* ── Endpoint 0 ────────────────────────────────────────────────── */
/* Ready for the next SETUP and for one OUT packet (data or status stage) */
static void ep0_arm_out(void) {
    WR(DOEPTSIZ(0), TSIZ_STUPCNT3 | TSIZ_PKTCNT1 | MPS);
    SETB(DOEPCTL(0), EPCTL_EPENA | EPCTL_CNAK);
}

static void ep0_send_next(void) {
    unsigned n = s_ep0_left > MPS ? MPS : s_ep0_left;
    WR(DIEPTSIZ(0), TSIZ_PKTCNT1 | n);
    SETB(DIEPCTL(0), EPCTL_EPENA | EPCTL_CNAK);
    fifo_write(0, s_ep0_tx, n);
    s_ep0_tx += n;
    s_ep0_left -= (uint16_t)n;
}

/* IN data stage: at most what the host asked for. A reply shorter than
 * that which ends on a full packet needs a ZLP to end the stage. */
static void ep0_send(const uint8_t *data, uint16_t len) {
    uint16_t wlength = (uint16_t)(s_setup[6] | (s_setup[7] << 8));
    if (len > wlength) len = wlength;
    s_ep0_tx   = data;
    s_ep0_left = len;
    s_ep0_zlp  = len < wlength && len % MPS == 0 && len > 0;
    ep0_send_next();
}

static void ep0_status_in(void) {               /* zero-length status packet */
    s_ep0_left = 0;
    s_ep0_zlp  = false;
    WR(DIEPTSIZ(0), TSIZ_PKTCNT1);
    SETB(DIEPCTL(0), EPCTL_EPENA | EPCTL_CNAK);
}

/* Unsupported request: STALL both directions; the core clears it on
 * the next SETUP */
static void ep0_stall(void) {
    SETB(DIEPCTL(0), EPCTL_STALL);
    SETB(DOEPCTL(0), EPCTL_STALL);
}

/* ── Data endpoints ────────────────────────────────────────────── */
static void ep_in_disable(unsigned ep) {
    if (RD(DIEPCTL(ep)) & EPCTL_EPENA) {
        SETB(DIEPCTL(ep), EPCTL_SNAK);
        SETB(DIEPCTL(ep), EPCTL_EPDIS);
        for (uint32_t n = 0; n < 200000U && !(RD(DIEPINT(ep)) & EPINT_EPDISD); n++) {}
        WR(DIEPINT(ep), EPINT_EPDISD);
    }
    flush_tx(ep);
}

static void rx_arm(void) {
    WR(DOEPTSIZ(EP_DATA), TSIZ_PKTCNT1 | MPS);
    SETB(DOEPCTL(EP_DATA), EPCTL_EPENA | EPCTL_CNAK);
    s_rx_armed = true;
}

/* Arm EP1 OUT if a whole packet fits in the receive ring. Interrupt
 * context, or main context with interrupts off. */
static void rx_maybe_arm(void) {
    if (s_config && !s_rx_armed && usb_cdc_rx_free() >= (int)MPS) rx_arm();
}

/* Send the next packet from the transmit ring (or a ZLP after a full
 * packet when the ring is empty). Same context rules as rx_maybe_arm. */
static void tx_maybe_send(void) {
    if (!s_config || s_tx_busy) return;
    uint8_t pkt[MPS];
    int n = usb_cdc_tx_pop(pkt, MPS);
    if (n == 0) {
        if (!s_tx_zlp) return;
        s_tx_zlp = false;
    } else {
        s_tx_zlp = (n == (int)MPS);
    }
    s_tx_busy = true;
    WR(DIEPTSIZ(EP_DATA), TSIZ_PKTCNT1 | (uint32_t)n);
    SETB(DIEPCTL(EP_DATA), EPCTL_EPENA | EPCTL_CNAK);
    fifo_write(EP_DATA, pkt, (unsigned)n);
}

static void deconfigure(void) {
    if (s_config) {
        ep_in_disable(EP_DATA);
        ep_in_disable(EP_NOTIFY);
        CLRB(DIEPCTL(EP_DATA), EPCTL_USBAEP);
        CLRB(DIEPCTL(EP_NOTIFY), EPCTL_USBAEP);
        SETB(DOEPCTL(EP_DATA), EPCTL_SNAK);
        CLRB(DOEPCTL(EP_DATA), EPCTL_USBAEP);
    }
    s_config = 0;
    s_tx_busy = false;
    s_tx_zlp = false;
    s_rx_armed = false;
    s_dtr = false;
}

static void configure(void) {
    WR(DIEPCTL(EP_DATA), EPCTL_USBAEP | EPCTL_EPTYP(EPTYP_BULK) |
                         EPCTL_TXFNUM(EP_DATA) | EPCTL_SD0PID | EPCTL_SNAK | MPS);
    WR(DOEPCTL(EP_DATA), EPCTL_USBAEP | EPCTL_EPTYP(EPTYP_BULK) |
                         EPCTL_SD0PID | EPCTL_SNAK | MPS);
    WR(DIEPCTL(EP_NOTIFY), EPCTL_USBAEP | EPCTL_EPTYP(EPTYP_INTR) |
                           EPCTL_TXFNUM(EP_NOTIFY) | EPCTL_SD0PID | EPCTL_SNAK | NOTIFY_MPS);
    SETB(DAINTMSK, (1U << EP_DATA) | (1U << (16 + EP_DATA)));
    s_config = 1;
    s_rx_armed = false;
    rx_maybe_arm();
}

/* ── Control requests ──────────────────────────────────────────── */
static uint16_t string_desc(unsigned index, uint8_t *out) {
    char serial[25];
    const char *s;
    if (index == 0) {
        out[0] = 4; out[1] = 3; out[2] = 0x09; out[3] = 0x04;   /* English (US) */
        return 4;
    }
    if (index >= sizeof(STRINGS) / sizeof(STRINGS[0])) return 0;
    s = STRINGS[index];
    if (index == 3) {
#ifdef USB_SIM
        const uint8_t uid[12] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 1, 2, 3, 4 };
#else
        const volatile uint8_t *uid = (const volatile uint8_t *)0x1FF07A10UL;   /* 96-bit UID */
#endif
        for (int i = 0; i < 12; i++) {
            serial[2 * i]     = "0123456789ABCDEF"[uid[i] >> 4];
            serial[2 * i + 1] = "0123456789ABCDEF"[uid[i] & 15];
        }
        serial[24] = 0;
        s = serial;
    }
    unsigned n = (unsigned)strlen(s);
    if (n > 31) n = 31;
    out[0] = (uint8_t)(2 + 2 * n);
    out[1] = 3;
    for (unsigned i = 0; i < n; i++) { out[2 + 2 * i] = (uint8_t)s[i]; out[3 + 2 * i] = 0; }
    return (uint16_t)(2 + 2 * n);
}

static bool ep_halted(unsigned addr) {
    unsigned ep = addr & 0x7FU;
    if (ep != EP_DATA && ep != EP_NOTIFY) return false;
    return (addr & 0x80U) ? (RD(DIEPCTL(ep)) & EPCTL_STALL) != 0
                          : (RD(DOEPCTL(ep)) & EPCTL_STALL) != 0;
}

static bool ep_set_halt(unsigned addr, bool halt) {
    unsigned ep = addr & 0x7FU;
    if (ep == 0) return true;
    if (!s_config || (ep != EP_DATA && ep != EP_NOTIFY) || (ep == EP_NOTIFY && !(addr & 0x80U)))
        return false;
    uint32_t reg = (addr & 0x80U) ? DIEPCTL(ep) : DOEPCTL(ep);
    if (halt) SETB(reg, EPCTL_STALL);
    else      WR(reg, (RD(reg) & ~(EPCTL_STALL | EPCTL_EPENA | EPCTL_EPDIS)) | EPCTL_SD0PID);
    return true;
}

static void handle_setup(void) {
    uint8_t  type = s_setup[0], req = s_setup[1];
    uint16_t value = (uint16_t)(s_setup[2] | (s_setup[3] << 8));
    uint16_t index = (uint16_t)(s_setup[4] | (s_setup[5] << 8));
    uint16_t length = (uint16_t)(s_setup[6] | (s_setup[7] << 8));
    s_ep0_out_req = 0;

    if ((type & 0x60U) == 0x00U) {                           /* standard */
        switch (req) {
        case 0x06:                                           /* GET_DESCRIPTOR */
            if (!(type & 0x80U)) break;
            switch (value >> 8) {
            case 1: ep0_send(DEVICE_DESC, sizeof(DEVICE_DESC)); return;
            case 2: ep0_send(CONFIG_DESC, sizeof(CONFIG_DESC)); return;
            case 3: {
                uint16_t n = string_desc(value & 0xFFU, s_ctrl_buf);
                if (n) { ep0_send(s_ctrl_buf, n); return; }
                break;
            }
            default: break;                                  /* qualifier etc: full speed only */
            }
            break;
        case 0x05:                                           /* SET_ADDRESS */
            WR(DCFG, (RD(DCFG) & ~DCFG_DAD_MASK) | ((uint32_t)(value & 0x7FU) << 4));
            ep0_status_in();
            return;
        case 0x09:                                           /* SET_CONFIGURATION */
            if (value > 1) break;
            deconfigure();
            if (value == 1) configure();
            ep0_status_in();
            return;
        case 0x08:                                           /* GET_CONFIGURATION */
            s_ctrl_buf[0] = s_config;
            ep0_send(s_ctrl_buf, 1);
            return;
        case 0x00:                                           /* GET_STATUS */
            s_ctrl_buf[0] = ((type & 0x1FU) == 2 && ep_halted(index)) ? 1 : 0;
            s_ctrl_buf[1] = 0;
            ep0_send(s_ctrl_buf, 2);
            return;
        case 0x01: case 0x03:                                /* CLEAR/SET_FEATURE */
            if ((type & 0x1FU) == 2 && value == 0) {         /* ENDPOINT_HALT */
                if (!ep_set_halt(index, req == 0x03)) break;
            } else if ((type & 0x1FU) != 0) {
                break;                                       /* nothing else on interfaces */
            }
            ep0_status_in();                                 /* device features: accepted */
            return;
        case 0x0A:                                           /* GET_INTERFACE */
            if (!s_config || index > 1) break;
            s_ctrl_buf[0] = 0;
            ep0_send(s_ctrl_buf, 1);
            return;
        case 0x0B:                                           /* SET_INTERFACE */
            if (!s_config || index > 1 || value != 0) break;
            ep0_status_in();
            return;
        default: break;
        }
    } else if ((type & 0x60U) == 0x20U && (type & 0x1FU) == 1 && index == 0) {   /* CDC class */
        switch (req) {
        case 0x20:                                           /* SET_LINE_CODING */
            if (length != 7) break;
            s_ep0_out_req = req;
            s_ep0_out_len = length;
            return;                                          /* data stage first */
        case 0x21:                                           /* GET_LINE_CODING */
            ep0_send(s_line_coding, 7);
            return;
        case 0x22:                                           /* SET_CONTROL_LINE_STATE */
            s_dtr = (value & 1U) != 0;
            ep0_status_in();
            return;
        case 0x23:                                           /* SEND_BREAK */
            ep0_status_in();
            return;
        default: break;
        }
    }
    ep0_stall();
}

/* ── Bus events ────────────────────────────────────────────────── */
static void on_reset(void) {
    CLRB(DCTL, DCTL_RWUSIG);
    deconfigure();
    flush_tx(0x10);
    for (unsigned ep = 0; ep < 4; ep++) {
        WR(DIEPINT(ep), 0xFFU);
        WR(DOEPINT(ep), 0xFFU);
    }
    WR(DAINTMSK, (1U << 0) | (1U << 16));
    WR(DOEPMSK, EPINT_XFRC | EPINT_STUP);
    WR(DIEPMSK, EPINT_XFRC);
    CLRB(DCFG, DCFG_DAD_MASK);
    s_ep0_left = 0;
    s_ep0_zlp = false;
    s_ep0_out_req = 0;
    ep0_arm_out();
}

static void on_rx(void) {
    uint32_t st = RD(GRXSTSP);
    unsigned ep = st & 0xFU, n = (st >> 4) & 0x7FFU, kind = (st >> 17) & 0xFU;
    if (kind == PKTSTS_SETUP_DATA) {
        if (n == 8) fifo_read(s_setup, 8); else fifo_discard(n);
        /* A new SETUP cancels whatever EP0 was still sending */
        s_ep0_left = 0;
        s_ep0_zlp = false;
        if (RD(DIEPTSIZ(0)) & (3U << 19)) {                  /* packet still queued */
            SETB(DIEPCTL(0), EPCTL_SNAK);
            flush_tx(0);
        }
    } else if (kind == PKTSTS_OUT_DATA) {
        if (ep == 0) {
            if (s_ep0_out_req && n <= sizeof(s_ctrl_buf)) fifo_read(s_ctrl_buf, n);
            else fifo_discard(n);
        } else if (ep == EP_DATA) {
            uint8_t pkt[MPS];
            if (n > MPS) { fifo_discard(n); return; }
            fifo_read(pkt, n);
            usb_cdc_rx_push(pkt, (int)n);    /* room was checked before arming */
        } else {
            fifo_discard(n);
        }
    }
    /* Other statuses (transfer/SETUP complete, global NAK) carry no data */
}

static void on_out_ep(void) {
    uint32_t daint = RD(DAINT) & RD(DAINTMSK);
    if (daint & (1U << 16)) {                                 /* EP0 OUT */
        uint32_t f = RD(DOEPINT(0));
        WR(DOEPINT(0), f);
        if ((f & EPINT_XFRC) && s_ep0_out_req) {
            if (s_ep0_out_req == 0x20) memcpy(s_line_coding, s_ctrl_buf, 7);
            s_ep0_out_req = 0;
            ep0_status_in();
        }
        if (f & EPINT_STUP) handle_setup();
        if (f & (EPINT_XFRC | EPINT_STUP)) ep0_arm_out();
    }
    if (daint & (1U << (16 + EP_DATA))) {                     /* EP1 OUT */
        uint32_t f = RD(DOEPINT(EP_DATA));
        WR(DOEPINT(EP_DATA), f);
        if (f & EPINT_XFRC) {
            s_rx_armed = false;
            rx_maybe_arm();
        }
    }
}

static void on_in_ep(void) {
    uint32_t daint = RD(DAINT) & RD(DAINTMSK);
    if (daint & (1U << 0)) {                                  /* EP0 IN */
        uint32_t f = RD(DIEPINT(0));
        WR(DIEPINT(0), f);
        if (f & EPINT_XFRC) {
            if (s_ep0_left) ep0_send_next();
            else if (s_ep0_zlp) ep0_status_in();
        }
    }
    if (daint & (1U << EP_DATA)) {                            /* EP1 IN */
        uint32_t f = RD(DIEPINT(EP_DATA));
        WR(DIEPINT(EP_DATA), f);
        if (f & EPINT_XFRC) {
            s_tx_busy = false;
            tx_maybe_send();
        }
    }
}

void usb_device_irq(void) {
    uint32_t sts = RD(GINTSTS) & RD(GINTMSK);
    if (sts & GINT_USBRST)  { WR(GINTSTS, GINT_USBRST); on_reset(); }
    if (sts & GINT_ENUMDNE) {
        WR(GINTSTS, GINT_ENUMDNE);
        WR(DIEPCTL(0), RD(DIEPCTL(0)) & ~3U);                /* MPSIZ = 64 bytes */
        SETB(DCTL, DCTL_CGINAK);
    }
    while (RD(GINTSTS) & GINT_RXFLVL) on_rx();
    if (sts & GINT_OEPINT) on_out_ep();
    if (sts & GINT_IEPINT) on_in_ep();
    if (sts & GINT_OTGINT) {
        uint32_t o = RD(GOTGINT);
        WR(GOTGINT, o);
        if (o & GOTGINT_SEDET) deconfigure();                /* VBUS gone */
    }
    if (sts & (GINT_USBSUSP | GINT_WKUPINT)) WR(GINTSTS, sts & (GINT_USBSUSP | GINT_WKUPINT));
}

#ifndef USB_SIM
void OTG_FS_IRQHandler(void) { usb_device_irq(); }
#endif

/* ── Set-up and main-loop side ─────────────────────────────────── */
void usb_device_init(void) {
#ifndef USB_SIM
    RCC->AHB2ENR |= (1U << 7);                               /* OTGFSEN */
    RCC_DCKCFGR2 &= ~RCC_DCKCFGR2_CK48MSEL;                  /* 48 MHz from PLLQ */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB2ENR;
    gpio_af(GPIOA, 11, 10U, 3U);                             /* D- */
    gpio_af(GPIOA, 12, 10U, 3U);                             /* D+ */
#endif
    s_ready = false;
    for (uint32_t n = 0; n < 200000U && !(RD(GRSTCTL) & GRSTCTL_AHBIDL); n++) {}
    WR(GRSTCTL, GRSTCTL_CSRST);                              /* core soft reset */
    if (!wait_clear(GRSTCTL, GRSTCTL_CSRST)) return;
    for (uint32_t n = 0; n < 200000U && !(RD(GRSTCTL) & GRSTCTL_AHBIDL); n++) {}

    WR(GCCFG, GCCFG_PWRDWN | GCCFG_VBDEN);                   /* PHY on, VBUS sensing */
    WR(GUSBCFG, (RD(GUSBCFG) & ~(GUSBCFG_TRDT_MASK | GUSBCFG_FHMOD)) |
                GUSBCFG_FDMOD | GUSBCFG_TRDT(6));            /* device, AHB >= 32 MHz */
#ifndef USB_SIM
    hal_delay_ms(25);                                        /* forced mode takes effect */
#endif
    WR(PCGCCTL, 0);
    WR(DCTL, DCTL_SDIS);                                     /* disconnected until VBUS */
    WR(DCFG, (RD(DCFG) & ~(3U | DCFG_DAD_MASK)) | DCFG_DSPD_FS);

    /* FIFO RAM is 320 words: RX 128, TX0 16, TX1 128 (two packets),
     * TX2 16 */
    WR(GRXFSIZ, 128);
    WR(DIEPTXF0, (16U << 16) | 128U);
    WR(DIEPTXF(1), (128U << 16) | 144U);
    WR(DIEPTXF(2), (16U << 16) | 272U);
    flush_tx(0x10);
    WR(GRSTCTL, GRSTCTL_RXFFLSH);
    wait_clear(GRSTCTL, GRSTCTL_RXFFLSH);

    WR(DIEPMSK, 0);
    WR(DOEPMSK, 0);
    WR(DAINTMSK, 0);
    for (unsigned ep = 0; ep < 4; ep++) {
        WR(DIEPINT(ep), 0xFFU);
        WR(DOEPINT(ep), 0xFFU);
    }
    WR(GINTSTS, 0xFFFFFFFFU);
    WR(GINTMSK, GINT_USBRST | GINT_ENUMDNE | GINT_RXFLVL | GINT_IEPINT |
                GINT_OEPINT | GINT_OTGINT | GINT_USBSUSP | GINT_WKUPINT);
    WR(GAHBCFG, GAHBCFG_GINTMSK);
    s_connected = false;
    deconfigure();
    s_ready = true;
#ifndef USB_SIM
    nvic_enable(67);                                         /* OTG_FS */
#endif
}

void usb_device_poll(void) {
    if (!s_ready) return;
    bool vbus = VBUS();
    if (vbus != s_connected) {
        uint32_t irq = IRQ_OFF();
        s_connected = vbus;
        if (vbus) CLRB(DCTL, DCTL_SDIS);                     /* D+ pull-up on: host sees us */
        else { SETB(DCTL, DCTL_SDIS); deconfigure(); }
        IRQ_ON(irq);
    }
    if (!s_config) return;
    uint32_t irq = IRQ_OFF();
    rx_maybe_arm();
    tx_maybe_send();
    IRQ_ON(irq);
}

bool usb_device_configured(void) { return s_config != 0; }
