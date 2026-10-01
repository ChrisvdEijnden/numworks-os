/* ================================================================
 * FatFs ff.h — Minimal stub for NumWorks OS build
 * Provides all types and declarations used in this project.
 * Replace ff.c + ff.h with real Chan FatFs R0.15 for full
 * USB FAT32 support: http://elm-chan.org/fsw/ff/
 * ================================================================ */
#pragma once
#include <stdint.h>

/* Basic types */
typedef uint32_t LBA_t;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint8_t  BYTE;
typedef unsigned int UINT;
typedef DWORD FSIZE_t;

/* FatFs result codes */
typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

/* File attribute flags */
#define AM_RDO  0x01
#define AM_HID  0x02
#define AM_SYS  0x04
#define AM_DIR  0x10
#define AM_ARC  0x20

/* File open mode flags */
#define FA_READ         0x01
#define FA_WRITE        0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW   0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS  0x10
#define FA_OPEN_APPEND  0x30

/* Max path length */
#define FF_MAX_LFN 255

/* File info structure */
typedef struct {
    DWORD   fsize;          /* File size */
    WORD    fdate;          /* Modified date */
    WORD    ftime;          /* Modified time */
    BYTE    fattrib;        /* File attribute */
    char    altname[13];    /* Alternative file name */
    char    fname[FF_MAX_LFN + 1]; /* Primary file name */
} FILINFO;

/* Filesystem object */
typedef struct {
    BYTE    fs_type;
    BYTE    pdrv;
    BYTE    n_fats;
    BYTE    wflag;
    WORD    n_rootdir;
    WORD    csize;
    DWORD   last_clst;
    DWORD   free_clst;
    DWORD   n_fatent;
    DWORD   fsize;
    LBA_t   volbase;
    LBA_t   fatbase;
    LBA_t   dirbase;
    LBA_t   database;
    LBA_t   winsect;
    BYTE    win[512];
} FATFS;

/* File object */
typedef struct {
    FATFS   *fs;
    WORD    id;
    BYTE    flag;
    BYTE    err;
    DWORD   fptr;
    DWORD   clust;
    LBA_t   sect;
    DWORD   fsize;
    DWORD   sclust;
    LBA_t   dir_sect;
    BYTE    *dir_ptr;
    BYTE    buf[512];
} FIL;

/* Directory object */
typedef struct {
    FATFS   *fs;
    WORD    id;
    WORD    index;
    DWORD   sclust;
    DWORD   clust;
    LBA_t   sect;
    BYTE    *dir;
    BYTE    *fn;
    BYTE    buf[512];
} DIR;

/* FatFs API */
FRESULT f_mount    (FATFS *fs, const char *path, BYTE opt);
FRESULT f_unmount  (const char *path);
FRESULT f_open     (FIL *fp, const char *path, BYTE mode);
FRESULT f_close    (FIL *fp);
FRESULT f_read     (FIL *fp, void *buf, UINT btr, UINT *br);
FRESULT f_write    (FIL *fp, const void *buf, UINT btw, UINT *bw);
FRESULT f_lseek    (FIL *fp, DWORD ofs);
FRESULT f_opendir  (DIR *dp, const char *path);
FRESULT f_closedir (DIR *dp);
FRESULT f_readdir  (DIR *dp, FILINFO *fno);
FRESULT f_stat     (const char *path, FILINFO *fno);
FRESULT f_unlink   (const char *path);
FRESULT f_mkdir    (const char *path);
FRESULT f_rename   (const char *path_old, const char *path_new);
FRESULT f_getfree  (const char *path, DWORD *nclst, FATFS **fatfs);
