/* ================================================================
 * NumWorks OS — USB CDC-ACM (Virtual Serial Port)
 * File: usb/usb_cdc.c
 *
 * File transfer protocol spoken over the CDC serial link (one command
 * per line, "\n" or "\r\n"; replies end in "\r\n"):
 *
 *   LIST               -> "<name> <size>" per file, then "OK"
 *   RECV <name>        -> "DATA <size>", <size> raw bytes, then "OK"
 *   SEND <name> <size> -> "READY"; then send <size> raw bytes -> "OK"
 *   DEL <name>         -> "OK"
 *   Any failure        -> "ERR <reason>"
 *
 * The PC side is tools/upload.py (and tools/transfer.py).
 *
 * The protocol only talks to two ring buffers. A USB device stack moves
 * bytes in and out of them with usb_cdc_rx_push() / usb_cdc_tx_pop().
 * NOTE: that stack does not exist yet — the OTG_FS core is only clocked
 * and pinned out below; enumeration, descriptors and endpoints still
 * have to be written (or ST's USB device library linked in).
 * ================================================================ */
#include "usb_cdc.h"
#include "../fs/flashfs.h"
#include "../hal/hal.h"
#include "../include/stm32f730.h"
#include "../include/config.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

/* ── OTG_FS register map (simplified) ───────────────────────── */
#define OTG_FS_BASE 0x50000000UL
typedef struct {
    volatile uint32_t GOTGCTL;  /* 000 */
    volatile uint32_t GOTGINT;  /* 004 */
    volatile uint32_t GAHBCFG; /* 008 */
    volatile uint32_t GUSBCFG; /* 00C */
    volatile uint32_t GRSTCTL; /* 010 */
    volatile uint32_t GINTSTS; /* 014 */
    volatile uint32_t GINTMSK; /* 018 */
    volatile uint32_t GRXSTSR; /* 01C */
    volatile uint32_t GRXSTSP; /* 020 */
    volatile uint32_t GRXFSIZ; /* 024 */
    volatile uint32_t DIEPTXF0;/* 028 */
    /* ... additional regs ... */
} OTG_FS_TypeDef;
#define OTG_FS ((OTG_FS_TypeDef *)OTG_FS_BASE)

/* ── Ring buffers between the USB stack and the protocol ────── */
static uint8_t  s_txbuf[USB_TX_BUFSIZE];
static uint8_t  s_rxbuf[USB_RX_BUFSIZE];
static uint16_t s_tx_head = 0, s_tx_tail = 0;
static uint16_t s_rx_head = 0, s_rx_tail = 0;

/* ── Hardware init ───────────────────────────────────────────── */
void usb_cdc_init(void) {
    /* Enable OTG_FS clock */
    RCC->AHB2ENR |= (1U << 7);  /* OTGFSEN */

    /* PA11 = DM (AF10), PA12 = DP (AF10) */
    GPIOA->MODER &= ~((3U<<22)|(3U<<24));
    GPIOA->MODER |=   (2U<<22)|(2U<<24);
    GPIOA->AFR[1] &= ~(0xFF << 12);
    GPIOA->AFR[1] |=  (0xAA << 12);  /* AF10 */

    /* Force device mode, disable VBUS sensing */
    OTG_FS->GUSBCFG |= (1U<<30) | (1U<<20);  /* FDMOD | PVBUS */

    /* Global interrupt mask — handled in process() by polling */
    OTG_FS->GAHBCFG = 0;  /* Polling mode (no DMA) */

    /*
     * Full USB CDC enumeration requires descriptor tables and
     * a complete control-transfer state machine (~4 KB).
     * For production: link STM32 USB Device Library and call
     * USBD_Init() / USBD_RegisterClass(&USBD_CDC) here.
     *
     * The protocol handler below works with any CDC implementation.
     */
}

int usb_cdc_tx_free(void) {
    return (int)((s_tx_head - s_tx_tail - 1 + USB_TX_BUFSIZE) % USB_TX_BUFSIZE);
}

/* Queue bytes for the PC; returns how many fitted */
int usb_cdc_write(const void *buf, int len) {
    const uint8_t *p = (const uint8_t *)buf;
    int written = 0;
    while (written < len) {
        uint16_t next = (s_tx_tail + 1) % USB_TX_BUFSIZE;
        if (next == s_tx_head) break;  /* Full */
        s_txbuf[s_tx_tail] = p[written++];
        s_tx_tail = next;
    }
    return written;
}

int usb_cdc_available(void) {
    return (s_rx_tail - s_rx_head + USB_RX_BUFSIZE) % USB_RX_BUFSIZE;
}

int usb_cdc_read(void *buf, int maxlen) {
    uint8_t *p = (uint8_t *)buf;
    int n = 0;
    while (n < maxlen && s_rx_head != s_rx_tail) {
        p[n++] = s_rxbuf[s_rx_head];
        s_rx_head = (s_rx_head + 1) % USB_RX_BUFSIZE;
    }
    return n;
}

/* ── Hooks for the USB device stack ──────────────────────────── */
int usb_cdc_rx_push(const void *buf, int len) {
    const uint8_t *p = (const uint8_t *)buf;
    int n = 0;
    while (n < len) {
        uint16_t next = (s_rx_tail + 1) % USB_RX_BUFSIZE;
        if (next == s_rx_head) break;  /* Full: the stack should NAK */
        s_rxbuf[s_rx_tail] = p[n++];
        s_rx_tail = next;
    }
    return n;
}

int usb_cdc_tx_pop(void *buf, int maxlen) {
    uint8_t *p = (uint8_t *)buf;
    int n = 0;
    while (n < maxlen && s_tx_head != s_tx_tail) {
        p[n++] = s_txbuf[s_tx_head];
        s_tx_head = (s_tx_head + 1) % USB_TX_BUFSIZE;
    }
    return n;
}

/* ── File transfer protocol ──────────────────────────────────── */
#define LINE_MAX       64
#define REPLY_MAX      64       /* room needed before taking a command */
#define SEND_TIMEOUT_MS 3000    /* SEND aborts if data stops this long */

typedef enum { ST_LINE, ST_LIST, ST_RECV, ST_SEND } proto_state_t;
static proto_state_t s_st = ST_LINE;

static char    s_line[LINE_MAX];
static uint8_t s_len = 0;
static bool    s_overflow = false;
static bool    s_skip_lf = false;   /* line ended in '\r': drop a following '\n' */

/* Read received bytes, dropping the '\n' of a "\r\n" line ending. It
 * must not end up as the first byte of SEND data. */
static int rx_take(uint8_t *buf, int max) {
    if (s_skip_lf && usb_cdc_available()) {
        uint8_t c;
        usb_cdc_read(&c, 1);
        s_skip_lf = false;
        if (c != '\n') { buf[0] = c; return 1 + usb_cdc_read(buf + 1, max - 1); }
    }
    return usb_cdc_read(buf, max);
}

/* LIST: snapshot of the directory, sent one line at a time */
static struct { char name[FFS_NAME_LEN]; uint32_t size; } s_list[FFS_MAX_FILES];
static int s_list_n = 0, s_list_i = 0;

/* RECV / SEND transfer */
static char     s_name[FFS_NAME_LEN];
static uint32_t s_off, s_size, s_pos;
static uint32_t s_last_rx_ms;
static uint8_t  s_file[FFS_MAX_FILE_SIZE];   /* SEND assembles the file here */

static void reply(const char *msg) {
    char buf[REPLY_MAX];
    int n = snprintf(buf, sizeof(buf), "%s\r\n", msg);
    usb_cdc_write(buf, n);
}

static void list_cb(const ffs_entry_t *e, void *ctx) {
    (void)ctx;
    if (s_list_n >= FFS_MAX_FILES) return;
    strncpy(s_list[s_list_n].name, e->name, FFS_NAME_LEN - 1);
    s_list[s_list_n].name[FFS_NAME_LEN - 1] = 0;
    s_list[s_list_n].size = e->size;
    s_list_n++;
}

static bool name_ok(const char *n) {
    size_t len = strlen(n);
    return len > 0 && len < FFS_NAME_LEN && !strchr(n, ' ');
}

static void handle_command(char *cmd) {
    if (strcmp(cmd, "LIST") == 0) {
        s_list_n = 0; s_list_i = 0;
        flashfs_ls(list_cb, NULL);
        s_st = ST_LIST;
    } else if (strncmp(cmd, "RECV ", 5) == 0) {
        const char *name = cmd + 5;
        if (flashfs_open_read(name, &s_off, &s_size) != 0) { reply("ERR not_found"); return; }
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "DATA %lu", (unsigned long)s_size);
        reply(hdr);
        s_pos = 0;
        s_st = ST_RECV;
    } else if (strncmp(cmd, "SEND ", 5) == 0) {
        char *sp = strrchr(cmd, ' ');            /* "SEND <name> <size>" */
        if (sp == cmd + 4) { reply("ERR usage: SEND <name> <size>"); return; }
        *sp = 0;
        char *end;
        unsigned long size = strtoul(sp + 1, &end, 10);
        const char *name = cmd + 5;
        if (*end || sp[1] == 0)          { reply("ERR bad_size");  return; }
        if (!name_ok(name))              { reply("ERR bad_name");  return; }
        if (size > FFS_MAX_FILE_SIZE)    { reply("ERR too_large"); return; }
        strncpy(s_name, name, FFS_NAME_LEN - 1);
        s_name[FFS_NAME_LEN - 1] = 0;
        s_size = (uint32_t)size; s_pos = 0;
        s_last_rx_ms = hal_tick_ms();
        s_st = ST_SEND;
        reply("READY");
    } else if (strncmp(cmd, "DEL ", 4) == 0) {
        reply(flashfs_delete(cmd + 4) == 0 ? "OK" : "ERR not_found");
    } else {
        reply("ERR unknown_command");
    }
}

/* Each step returns false when it can't progress (no input / TX full) */
static bool line_step(void) {
    if (usb_cdc_tx_free() < REPLY_MAX) return false;   /* room for a reply */
    uint8_t c;
    if (rx_take(&c, 1) != 1) return false;
    if (c == '\n' || c == '\r') {
        s_skip_lf = (c == '\r');
        if (s_overflow)     reply("ERR line_too_long");
        else if (s_len > 0) { s_line[s_len] = 0; handle_command(s_line); }
        s_len = 0; s_overflow = false;
    } else if (s_len < LINE_MAX - 1) {
        s_line[s_len++] = (char)c;
    } else {
        s_overflow = true;
    }
    return true;
}

static bool list_step(void) {
    char line[FFS_NAME_LEN + 16];
    if (s_list_i >= s_list_n) {
        if (usb_cdc_tx_free() < 4) return false;
        reply("OK");
        s_st = ST_LINE;
        return true;
    }
    int n = snprintf(line, sizeof(line), "%s %lu\r\n",
                     s_list[s_list_i].name, (unsigned long)s_list[s_list_i].size);
    if (usb_cdc_tx_free() < n) return false;
    usb_cdc_write(line, n);
    s_list_i++;
    return true;
}

static bool recv_step(void) {
    if (s_pos >= s_size) {
        if (usb_cdc_tx_free() < 4) return false;
        reply("OK");
        s_st = ST_LINE;
        return true;
    }
    uint8_t chunk[64];
    uint32_t n = s_size - s_pos;
    uint32_t space = (uint32_t)usb_cdc_tx_free();
    if (n > space) n = space;
    if (n > sizeof(chunk)) n = sizeof(chunk);
    if (n == 0) return false;
    if (flashfs_read(s_off + s_pos, chunk, n) != (int)n) {
        /* Can't happen for a file that open_read just returned; don't
         * leave the PC waiting for bytes that never come */
        s_st = ST_LINE;
        return true;
    }
    usb_cdc_write(chunk, (int)n);
    s_pos += n;
    return true;
}

static bool send_step(void) {
    if (s_pos < s_size) {
        int n = rx_take(s_file + s_pos, (int)(s_size - s_pos));
        if (n > 0) {
            s_pos += (uint32_t)n;
            s_last_rx_ms = hal_tick_ms();
            return true;
        }
        if (hal_tick_ms() - s_last_rx_ms > SEND_TIMEOUT_MS) {
            reply("ERR timeout");
            s_st = ST_LINE;
            return true;
        }
        return false;
    }
    if (usb_cdc_tx_free() < REPLY_MAX) return false;
    reply(flashfs_write(s_name, s_file, s_size) == (int)s_size ? "OK" : "ERR write_failed");
    s_st = ST_LINE;
    return true;
}

/* Called in main event loop */
void usb_cdc_process(void) {
    for (int guard = 0; guard < 1024; guard++) {   /* bounded work per call */
        bool progressed;
        switch (s_st) {
            case ST_LIST: progressed = list_step(); break;
            case ST_RECV: progressed = recv_step(); break;
            case ST_SEND: progressed = send_step(); break;
            default:      progressed = line_step(); break;
        }
        if (!progressed) return;
    }
}
