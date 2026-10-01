/* ================================================================
 * NumWorks OS — FatFs physical-drive layer
 * File: fs/diskio.c
 *
 * Drive 0: none. Internal storage is flashfs (fs/flashfs.c), which is
 *          not a FAT volume, so FatFs must never read or format it.
 * Drive 1: USB mass storage ("1:"), through the MSC hooks in
 *          usb/usb_host.c. These report "not ready" until a USB host
 *          stack with a mass-storage class driver exists.
 * ================================================================ */
#include "ff.h"
#include "diskio.h"
#include "../include/config.h"

#define DRV_USB 1

DSTATUS disk_initialize(BYTE drv) {
    if (drv != DRV_USB) return STA_NOINIT | STA_NODISK;
    return usb_msc_disk_status();
}

DSTATUS disk_status(BYTE drv) {
    if (drv != DRV_USB) return STA_NOINIT | STA_NODISK;
    return usb_msc_disk_status();
}

DRESULT disk_read(BYTE drv, BYTE *buf, LBA_t sect, UINT count) {
    if (drv != DRV_USB) return RES_PARERR;
    return usb_msc_disk_read(buf, sect, count);
}

DRESULT disk_write(BYTE drv, const BYTE *buf, LBA_t sect, UINT count) {
    if (drv != DRV_USB) return RES_PARERR;
    return usb_msc_disk_write(buf, sect, count);
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buf) {
    if (drv != DRV_USB) return RES_PARERR;
    return usb_msc_disk_ioctl(cmd, buf);
}

/* FatFs timestamps: there is no real-time clock, so files get a fixed
 * date (2024-01-01 00:00). Packed as FatFs expects. */
DWORD get_fattime(void) {
    return ((DWORD)(2024 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}
