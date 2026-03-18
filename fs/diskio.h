/* ================================================================
 * FatFs diskio.h — Physical drive interface declarations
 * Matches the implementation in diskio.c
 * ================================================================ */
#pragma once
#include "ff.h"

DSTATUS disk_initialize(BYTE drv);
DSTATUS disk_status    (BYTE drv);
DRESULT disk_read      (BYTE drv, BYTE *buf, LBA_t sect, UINT count);
DRESULT disk_write     (BYTE drv, const BYTE *buf, LBA_t sect, UINT count);
DRESULT disk_ioctl     (BYTE drv, BYTE cmd, void *buf);

/* Used by usb_host.c for drive 1 (USB MSC) */
DRESULT usb_msc_disk_read (BYTE *buf, LBA_t sector, UINT count);
DRESULT usb_msc_disk_write(const BYTE *buf, LBA_t sector, UINT count);
