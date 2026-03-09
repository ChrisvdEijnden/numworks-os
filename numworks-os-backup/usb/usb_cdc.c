/* ================================================================
 * NumWorks OS — USB CDC-ACM (Virtual Serial Port)
 * File: usb/usb_cdc.c
 *
 * Implements USB 2.0 Full-Speed CDC-ACM so the calculator appears
 * as a virtual COM port on a PC. The PC side uses a simple text
 * protocol to upload/download files:
 *
 *   SEND <name> <size>\n<data>   — upload file
 *   RECV <name>\n                — download file
 *   LIST\n                       — list files
 *   DEL  <name>\n                — delete file
 *
 * The USB peripheral on STM32F730 is OTG_FS.
 * A complete USB stack is large; this file shows the architecture
 * and key register initialisation. For a production build, link
 * against STM32 USB Device Library (ST's free LGPL stack).
 *
 * Code size target: < 8 KB (driver) + 1 KB (protocol handler)
 * ================================================================ */
#include "usb_cdc.h"
#include "../fs/flashfs.h"
#include "../shell/shell.h"
#include "../include/stm32f730.h"
#include "../include/config.h"
#include <string.h>
#include <stdio.h>

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

/* TX/RX software ring buffers */
static uint8_t s_txbuf[USB_TX_BUFSIZE];
static uint8_t s_rxbuf[USB_RX_BUFSIZE];
static uint16_t s_tx_head = 0, s_tx_tail = 0;
static uint16_t s_rx_head = 0, s_rx_tail = 0;

static bool s_connected = false;

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
    s_connected = false;
}

/* ── Write to TX buffer ──────────────────────────────────────── */
int usb_cdc_write(const void *buf, int len) {
    const uint8_t *p = (const uint8_t *)buf;
    int written = 0;
    while (len-- && written < (int)USB_TX_BUFSIZE) {
        uint16_t next = (s_tx_tail + 1) % USB_TX_BUFSIZE;
        if (next == s_tx_head) break;  /* Full */
        s_txbuf[s_tx_tail] = *p++;
        s_tx_tail = next;
        written++;
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

/* ── Simple text-protocol handler ───────────────────────────── */
static char    s_cmd_buf[64];
static uint8_t s_cmd_len = 0;

static void handle_command(const char *cmd) {
    if (strncmp(cmd, "LIST", 4) == 0) {
        /* Send file list */
        char line[64];
        /* Iterate flashfs and write each entry */
        shell_puts("[USB] LIST command received\n");
    } else if (strncmp(cmd, "DEL ", 4) == 0) {
        flashfs_delete(cmd + 4);
        usb_cdc_write("OK\r\n", 4);
    } else if (strncmp(cmd, "RECV ", 5) == 0) {
        const char *name = cmd + 5;
        uint32_t off, sz;
        if (flashfs_open_read(name, &off, &sz) == 0) {
            char hdr[32];
            int hlen = snprintf(hdr, sizeof(hdr), "DATA %lu\r\n", (unsigned long)sz);
            usb_cdc_write(hdr, hlen);
            /* Stream file data */
            static uint8_t fbuf[256];
            uint32_t sent = 0;
            while (sent < sz) {
                uint32_t chunk = sz - sent;
                if (chunk > sizeof(fbuf)) chunk = sizeof(fbuf);
                flashfs_read(off + sent, fbuf, chunk);
                usb_cdc_write(fbuf, (int)chunk);
                sent += chunk;
            }
        } else {
            usb_cdc_write("ERR not_found\r\n", 15);
        }
    }
    /* SEND protocol would need state machine — omitted for size */
}

/* Called in main event loop */
void usb_cdc_process(void) {
    /* Poll OTG_FS GRXSTSP for received data */
    /* In real implementation: ST HAL calls USBD_CDC_ReceivePacket */
    while (usb_cdc_available()) {
        uint8_t c;
        usb_cdc_read(&c, 1);
        if (c == '\n' || c == '\r') {
            if (s_cmd_len > 0) {
                s_cmd_buf[s_cmd_len] = 0;
                handle_command(s_cmd_buf);
                s_cmd_len = 0;
            }
        } else if (s_cmd_len < sizeof(s_cmd_buf) - 1) {
            s_cmd_buf[s_cmd_len++] = (char)c;
        }
    }
}
