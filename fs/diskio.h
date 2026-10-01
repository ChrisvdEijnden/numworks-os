/* ================================================================
 * FatFs diskio.h — Physical drive interface (as in FatFs' own diskio.h)
 * Kept when ff.c / ff.h / ffconf.h are replaced by the real FatFs.
 * ================================================================ */
#pragma once
#include "ff.h"

typedef BYTE DSTATUS;
typedef enum { RES_OK = 0, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR } DRESULT;

/* disk_status flags */
#define STA_NOINIT  0x01
#define STA_NODISK  0x02
#define STA_PROTECT 0x04

/* disk_ioctl commands */
#define CTRL_SYNC         0
#define GET_SECTOR_COUNT  1
#define GET_SECTOR_SIZE   2
#define GET_BLOCK_SIZE    3
#define CTRL_TRIM         4

DSTATUS disk_initialize(BYTE drv);
DSTATUS disk_status    (BYTE drv);
DRESULT disk_read      (BYTE drv, BYTE *buf, LBA_t sect, UINT count);
DRESULT disk_write     (BYTE drv, const BYTE *buf, LBA_t sect, UINT count);
DRESULT disk_ioctl     (BYTE drv, BYTE cmd, void *buf);
DWORD   get_fattime    (void);

/* Drive 1 (USB mass storage), implemented in usb/usb_host.c */
DSTATUS usb_msc_disk_status(void);
DRESULT usb_msc_disk_read  (BYTE *buf, LBA_t sector, UINT count);
DRESULT usb_msc_disk_write (const BYTE *buf, LBA_t sector, UINT count);
DRESULT usb_msc_disk_ioctl (BYTE cmd, void *buf);
