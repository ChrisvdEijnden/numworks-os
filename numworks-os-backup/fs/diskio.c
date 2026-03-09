/* ================================================================
 * NumWorks OS — FatFs diskio shim for flashfs
 * File: fs/diskio.c
 *
 * Maps FatFs physical layer to our flashfs for internal storage.
 * For external USB-MSC, a second drive (1:) would be wired here.
 * ================================================================ */
#include "ff.h"
#include "diskio.h"
#include "flashfs.h"
#include "../include/config.h"
#include <string.h>

/* Drive 0 = internal flash (512-byte logical sectors over flashfs) */
#define SECTOR_SIZE 512U
#define DISK_SIZE   (FFS_SIZE)

DSTATUS disk_initialize(BYTE drv) {
    if (drv != 0) return STA_NOINIT;
    return 0;
}

DSTATUS disk_status(BYTE drv) {
    if (drv != 0) return STA_NODISK;
    return 0;
}

DRESULT disk_read(BYTE drv, BYTE *buf, LBA_t sect, UINT count) {
    if (drv != 0) return RES_PARERR;
    uint32_t off = (uint32_t)(FFS_START + sect * SECTOR_SIZE);
    memcpy(buf, (const void *)off, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buf, LBA_t sect, UINT count) {
    if (drv != 0) return RES_PARERR;
    /* Flash writes handled by flashfs layer; this is for FAT metadata */
    (void)buf; (void)sect; (void)count;
    return RES_OK;  /* Simplified — full implementation would page-erase */
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buf) {
    if (drv != 0) return RES_PARERR;
    switch (cmd) {
        case CTRL_SYNC:      return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t*)buf = DISK_SIZE / SECTOR_SIZE; return RES_OK;
        case GET_SECTOR_SIZE:  *(WORD *)buf = SECTOR_SIZE;             return RES_OK;
        case GET_BLOCK_SIZE:   *(DWORD*)buf = 1;                       return RES_OK;
    }
    return RES_PARERR;
}
