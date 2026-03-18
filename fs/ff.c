/* ================================================================
 * FatFs ff.c — Stub implementation for NumWorks OS build
 *
 * All functions return FR_NOT_ENABLED until you replace this file
 * with real Chan FatFs R0.15 from http://elm-chan.org/fsw/ff/
 *
 * To use real FatFs:
 *   1. Download ff14b.zip (or latest) from elm-chan.org
 *   2. Copy ff.c, ff.h, ffconf.h into this fs/ directory
 *   3. Rebuild — diskio.c already provides the disk_* layer
 * ================================================================ */
#include "ff.h"
#include "../include/string.h"

FRESULT f_mount   (FATFS *fs, const char *path, BYTE opt)
    { (void)fs;(void)path;(void)opt; return FR_NOT_ENABLED; }
FRESULT f_unmount (const char *path)
    { (void)path; return FR_NOT_ENABLED; }
FRESULT f_open    (FIL *fp, const char *path, BYTE mode)
    { (void)fp;(void)path;(void)mode; return FR_NOT_ENABLED; }
FRESULT f_close   (FIL *fp)
    { (void)fp; return FR_NOT_ENABLED; }
FRESULT f_read    (FIL *fp, void *buf, UINT btr, UINT *br)
    { (void)fp;(void)buf;(void)btr; if(br)*br=0; return FR_NOT_ENABLED; }
FRESULT f_write   (FIL *fp, const void *buf, UINT btw, UINT *bw)
    { (void)fp;(void)buf;(void)btw; if(bw)*bw=0; return FR_NOT_ENABLED; }
FRESULT f_lseek   (FIL *fp, DWORD ofs)
    { (void)fp;(void)ofs; return FR_NOT_ENABLED; }
FRESULT f_opendir (DIR *dp, const char *path)
    { (void)dp;(void)path; return FR_NOT_ENABLED; }
FRESULT f_closedir(DIR *dp)
    { (void)dp; return FR_NOT_ENABLED; }
FRESULT f_readdir (DIR *dp, FILINFO *fno)
    { (void)dp; if(fno)memset(fno,0,sizeof(*fno)); return FR_NOT_ENABLED; }
FRESULT f_stat    (const char *path, FILINFO *fno)
    { (void)path;(void)fno; return FR_NOT_ENABLED; }
FRESULT f_unlink  (const char *path)
    { (void)path; return FR_NOT_ENABLED; }
FRESULT f_mkdir   (const char *path)
    { (void)path; return FR_NOT_ENABLED; }
FRESULT f_rename  (const char *o, const char *n)
    { (void)o;(void)n; return FR_NOT_ENABLED; }
FRESULT f_getfree (const char *path, DWORD *nclst, FATFS **fatfs)
    { (void)path;(void)nclst;(void)fatfs; return FR_NOT_ENABLED; }

/* Stub disk I/O for drive 1 (USB MSC) — wired up in usb_host.c */
DRESULT usb_msc_disk_read (BYTE *buf, LBA_t sect, UINT cnt)
    { (void)buf;(void)sect;(void)cnt; return RES_NOTRDY; }
DRESULT usb_msc_disk_write(const BYTE *buf, LBA_t sect, UINT cnt)
    { (void)buf;(void)sect;(void)cnt; return RES_NOTRDY; }
