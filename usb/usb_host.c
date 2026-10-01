
/* ================================================================
 * NumWorks OS — USB Host Driver (Mass Storage)
 * File: usb/usb_host.c
 *
 * Implementation notes:
 *  - Uses STM32F730 OTG_FS in Host mode (PA11=DM, PA12=DP)
 *  - Enumerates USB 2.0 FS Mass Storage Class devices
 *  - Mounts FAT32 partition via Chan FatFs
 *  - Provides ls / read_file / import operations
 *
 * Hardware note:
 *  Nothing on the board can supply 5 V on VBUS: the power path is the
 *  RT9526A linear charger (VBUS in, battery out) and the USBLC6-2 is
 *  passive ESD protection. Bus-powered drives therefore can't work; a
 *  self-powered drive needs a role-swap the USB-C port may not offer.
 *  The OTG_FS core is also used in device mode by usb_cdc.c, so host
 *  mode is only started on request, never at boot.
 *
 * This file implements the state machine and HAL register access.
 * ================================================================ */
#include "usb_host.h"
#include "../fs/ff.h"
#include "../include/stm32f730.h"
#include "../include/config.h"
#include "../include/string.h"
#include "../include/stdio.h"

/* OTG_FS register base (STM32F7) */
#define OTG_FS_BASE   0x50000000UL
#define OTG_GAHBCFG   (*(volatile uint32_t*)(OTG_FS_BASE + 0x008))
#define OTG_GUSBCFG   (*(volatile uint32_t*)(OTG_FS_BASE + 0x00C))
#define OTG_GRSTCTL   (*(volatile uint32_t*)(OTG_FS_BASE + 0x010))
#define OTG_GINTSTS   (*(volatile uint32_t*)(OTG_FS_BASE + 0x014))
#define OTG_GINTMSK   (*(volatile uint32_t*)(OTG_FS_BASE + 0x018))
#define OTG_HCFG      (*(volatile uint32_t*)(OTG_FS_BASE + 0x400))
#define OTG_HPRT      (*(volatile uint32_t*)(OTG_FS_BASE + 0x440))
#define OTG_GUSBCFG_FHMOD (1U<<29)
#define OTG_GUSBCFG_FDMOD (1U<<30)

/* HPRT bits that are cleared by writing 1 (PENA disables the port!).
 * Every read-modify-write of HPRT must mask them out. */
#define HPRT_PCSTS    (1U<<0)   /* device connected (read-only) */
#define HPRT_PCDET    (1U<<1)
#define HPRT_PENA     (1U<<2)
#define HPRT_PENCHNG  (1U<<3)
#define HPRT_POCCHNG  (1U<<5)
#define HPRT_W1C_MASK (HPRT_PCDET | HPRT_PENA | HPRT_PENCHNG | HPRT_POCCHNG)
#define HPRT_PRST     (1U<<8)
#define HPRT_PPWR     (1U<<12)

static void hprt_set(uint32_t bits) {
    OTG_HPRT = (OTG_HPRT & ~HPRT_W1C_MASK) | bits;
}
static void hprt_clear(uint32_t bits) {
    OTG_HPRT = OTG_HPRT & ~(HPRT_W1C_MASK | bits);
}

typedef enum {
    HOST_STATE_IDLE,
    HOST_STATE_WAIT_CONNECT,
    HOST_STATE_RESET,
    HOST_STATE_ENUMERATE,
    HOST_STATE_MOUNT,
    HOST_STATE_READY,
    HOST_STATE_ERROR
} host_state_t;

static host_state_t s_state   = HOST_STATE_IDLE;
static bool         s_mounted = false;
static FATFS        s_fatfs;

/* FatFs disk I/O layer — implemented in fs/diskio.c (USB MSC path) */
extern DRESULT  usb_msc_disk_read(BYTE *buf, LBA_t sector, UINT count);
extern DRESULT  usb_msc_disk_write(const BYTE *buf, LBA_t sector, UINT count);

/* ── OTG_FS clock & GPIO init ────────────────────────────────── */
static void otg_gpio_init(void) {
    /* Enable OTG_FS clock */
    RCC->AHB2ENR |= (1U << 7);   /* OTGFSEN */
    /* PA11 (DM), PA12 (DP) in AF10 */
    RCC->AHB1ENR |= (1U << 0);   /* GPIOAEN */
    volatile uint32_t *GPIOA_MODER  = (volatile uint32_t *)0x40020000UL;
    volatile uint32_t *GPIOA_AFRH   = (volatile uint32_t *)0x40020024UL;
    *GPIOA_MODER  = (*GPIOA_MODER  & ~((3U<<22)|(3U<<24))) | ((2U<<22)|(2U<<24));
    *GPIOA_AFRH   = (*GPIOA_AFRH   & ~((0xFUL<<12)|(0xFUL<<16))) | ((10UL<<12)|(10UL<<16));
}

int usb_host_init(void) {
    s_state   = HOST_STATE_IDLE;
    s_mounted = false;
    /* The core can't be host and device at once: leave it alone while
     * usb_cdc.c has it in forced device mode. */
    if (OTG_GUSBCFG & OTG_GUSBCFG_FDMOD) return -1;
    otg_gpio_init();
    /* Configure OTG FS as host */
    OTG_GUSBCFG |= OTG_GUSBCFG_FHMOD;
    for (volatile int i=0; i<50000; i++) {}
    OTG_HCFG = 1;               /* FS PHY clock 48 MHz */
    /* Enable port power */
    hprt_set(HPRT_PPWR);
    s_state = HOST_STATE_WAIT_CONNECT;
    return 0;
}

int usb_host_mounted(void) {
    return s_mounted ? 1 : 0;
}

/* State machine tick — call from main loop */
void usb_host_process(void) {
    switch (s_state) {
        case HOST_STATE_WAIT_CONNECT:
            /* Check port connect */
            if (OTG_HPRT & HPRT_PCSTS) {
                s_state = HOST_STATE_RESET;
            }
            break;
        case HOST_STATE_RESET:
            /* Issue port reset */
            hprt_set(HPRT_PRST);
            for (volatile int i=0; i<200000; i++) {}
            hprt_clear(HPRT_PRST);
            s_state = HOST_STATE_ENUMERATE;
            break;
        case HOST_STATE_ENUMERATE:
            /* Simplified: assume MSC device is present, attempt FAT mount */
            s_state = HOST_STATE_MOUNT;
            break;
        case HOST_STATE_MOUNT: {
            FRESULT fr = f_mount(&s_fatfs, "1:", 1);
            if (fr == FR_OK) {
                s_mounted = true;
                s_state   = HOST_STATE_READY;
            } else {
                s_state = HOST_STATE_ERROR;
            }
            break;
        }
        case HOST_STATE_READY:
            /* Check for disconnect */
            if (!(OTG_HPRT & HPRT_PCSTS)) {
                s_mounted = false;
                f_unmount("1:");
                s_state = HOST_STATE_WAIT_CONNECT;
            }
            break;
        default:
            break;
    }
}

int usb_host_ls(char names[][32], int maxn) {
    if (!s_mounted) return 0;
    DIR   dir;
    FILINFO fi;
    int n = 0;
    if (f_opendir(&dir, "1:/") != FR_OK) return 0;
    while (n < maxn) {
        if (f_readdir(&dir, &fi) != FR_OK || fi.fname[0]==0) break;
        if (fi.fattrib & AM_DIR) continue;
        /* Filter: only .txt, .py, .jpg, .png, .bmp */
        const char *ext = strrchr(fi.fname, '.');
        if (!ext) continue;
        if (strcasecmp(ext,".txt")!=0 && strcasecmp(ext,".py")!=0 &&
            strcasecmp(ext,".jpg")!=0 && strcasecmp(ext,".png")!=0 &&
            strcasecmp(ext,".bmp")!=0) continue;
        strncpy(names[n], fi.fname, 31);
        names[n][31] = 0;
        n++;
    }
    f_closedir(&dir);
    return n;
}

int usb_host_read_file(const char *name, uint8_t *buf,
                       uint32_t maxlen, uint32_t *size_out) {
    if (!s_mounted) return -1;
    char path[48];
    snprintf(path, sizeof(path), "1:/%s", name);
    FIL  fp;
    UINT br;
    if (f_open(&fp, path, FA_READ) != FR_OK) return -1;
    FRESULT fr = f_read(&fp, buf, maxlen, &br);
    f_close(&fp);
    if (fr != FR_OK) return -1;
    *size_out = br;
    return 0;
}

int usb_host_read_at(const char *name, uint32_t offset, uint8_t *buf,
                     uint32_t len, uint32_t *got) {
    if (!s_mounted) return -1;
    char path[48];
    snprintf(path, sizeof(path), "1:/%s", name);
    FIL  fp;
    UINT br = 0;
    if (f_open(&fp, path, FA_READ) != FR_OK) return -1;
    FRESULT fr = f_lseek(&fp, offset);
    if (fr == FR_OK) fr = f_read(&fp, buf, len, &br);
    f_close(&fp);
    if (fr != FR_OK) return -1;
    *got = br;
    return 0;
}

int usb_host_import(const char *usb_name, const char *dest_name) {
    static uint8_t buf[8192];
    uint32_t sz = 0;
    if (usb_host_read_file(usb_name, buf, sizeof(buf), &sz) < 0) return -1;
    extern int flashfs_write(const char *, const void *, uint32_t);
    return flashfs_write(dest_name, buf, sz);
}
