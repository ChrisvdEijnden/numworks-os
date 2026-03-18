/* ================================================================
 * NumWorks OS — Internal Flash Filesystem
 * File: fs/flashfs.h
 *
 * Layout in the 64 KB FS sector (sector 7 @ 0x0807_0000):
 *
 *  [0x000..0x0FF]  Superblock (256 bytes)
 *  [0x100..0xFFF]  Directory entries  (32 × 128 bytes = 4 KB)
 *  [0x1000..end]   File data pool     (~60 KB)
 *
 * Design goals: zero malloc, simple, crash-safe (2-copy super).
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../include/config.h"

#define FFS_MAGIC   0x4E574F53UL  /* "NWOS" */
#define FFS_VERSION 1

/* Directory entry (fits 32 in 4 KB) */
typedef struct __attribute__((packed)) {
    char     name[FFS_NAME_LEN];  /* Null-terminated filename    */
    uint32_t offset;              /* Offset from FFS_START       */
    uint32_t size;                /* File size in bytes          */
    uint32_t flags;               /* Bit 0 = valid, Bit 1 = dir  */
    uint32_t _pad;
} ffs_entry_t;                    /* = 40 bytes */

/* Superblock */
typedef struct __attribute__((packed)) {
    uint32_t   magic;
    uint8_t    version;
    uint8_t    _pad[3];
    uint32_t   num_entries;
    uint32_t   data_end;          /* Next free byte in data pool */
    uint32_t   crc;
} ffs_super_t;

/* Public API */
int  flashfs_init(void);
int  flashfs_format(void);

int  flashfs_open_read(const char *path, uint32_t *offset, uint32_t *size);
int  flashfs_read(uint32_t offset, void *buf, uint32_t len);
int  flashfs_write(const char *path, const void *data, uint32_t len);
int  flashfs_delete(const char *path);
int  flashfs_rename(const char *from, const char *to);
bool flashfs_exists(const char *path);

int  flashfs_ls(void (*cb)(const ffs_entry_t *e, void *ctx), void *ctx);
void flashfs_stats(uint32_t *used, uint32_t *free_bytes);
