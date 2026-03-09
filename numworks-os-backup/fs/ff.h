/* FatFs stub — replace with actual Chan FatFs R0.15 source.
 * Download from: http://elm-chan.org/fsw/ff/
 * Copy ff.h, ff.c, ffconf.h to this directory.          */
#pragma once
#include <stdint.h>
typedef uint32_t LBA_t;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint8_t  BYTE;
typedef unsigned int UINT;
typedef uint8_t DSTATUS;
typedef enum { RES_OK=0, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR } DRESULT;
#define STA_NOINIT  0x01
#define STA_NODISK  0x02
#define CTRL_SYNC         0
#define GET_SECTOR_COUNT  1
#define GET_SECTOR_SIZE   2
#define GET_BLOCK_SIZE    3
