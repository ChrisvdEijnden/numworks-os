/* ================================================================
 * NumWorks OS — Internal Flash Filesystem
 * File: fs/flashfs.h
 *
 * Layout in the 64 KB FS sector (sector 7 @ 0x0807_0000):
 *
 *  [0x000..0x0FF]  Superblock (written once, when formatting)
 *  [0x100..0xFFF]  Record log: 96 x 40-byte records, append-only
 *  [0x1000..end]   File data pool, append-only (~60 KB)
 *
 * Flash can only be programmed from the erased state, so nothing is
 * ever rewritten in place. Writing, deleting or renaming a file appends
 * a record to the log, and the newest record for a name wins. A record
 * only counts once its last word (the kind) is programmed, so a power
 * cut mid-write leaves the previous version of the file intact.
 *
 * When the log or the data pool is full, the live files are compacted
 * into a RAM scratch buffer, the sector is erased and the compacted
 * image is programmed back. A power cut during that step loses the
 * filesystem (it then needs formatting); avoiding that needs a second
 * sector to compact into.
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../include/config.h"

#define FFS_MAGIC   0x4E574F53UL  /* "NWOS" */
#define FFS_VERSION 2             /* 2 = append-only record log */

/* flashfs_init() / flashfs_format() results */
#define FFS_OK          0
#define FFS_ERR_FORMAT (-1)   /* no valid filesystem: format it */
#define FFS_ERR_NODEV  (-2)   /* FFS_START is not flash on this chip */

/* Directory entry, also the on-flash log record (40 bytes).
 * Entries handed out by flashfs_ls() have flags bit 0 set and an
 * absolute `offset` that can be passed to flashfs_read(). */
typedef struct __attribute__((packed)) {
    char     name[FFS_NAME_LEN];  /* Null-terminated filename    */
    uint32_t offset;              /* Data address                */
    uint32_t size;                /* File size in bytes          */
    uint32_t flags;               /* Bit 0 = valid / record kind */
    uint32_t _pad;
} ffs_entry_t;

/* Superblock */
typedef struct __attribute__((packed)) {
    uint32_t   magic;
    uint8_t    version;
    uint8_t    _pad[3];
} ffs_super_t;

/* Public API */
int  flashfs_init(void);     /* FFS_OK, FFS_ERR_FORMAT or FFS_ERR_NODEV */
int  flashfs_format(void);

/* RAM the filesystem may overwrite while compacting (at least FFS_SIZE
 * bytes). `released` is called afterwards, since the buffer's previous
 * contents are gone. Without a scratch buffer, writes fail once the
 * log or the data pool is full. */
void flashfs_set_scratch(void *buf, uint32_t len, void (*released)(void));

int  flashfs_open_read(const char *path, uint32_t *offset, uint32_t *size);
int  flashfs_read(uint32_t offset, void *buf, uint32_t len);
int  flashfs_write(const char *path, const void *data, uint32_t len);
int  flashfs_delete(const char *path);
int  flashfs_rename(const char *from, const char *to);
bool flashfs_exists(const char *path);

int  flashfs_ls(void (*cb)(const ffs_entry_t *e, void *ctx), void *ctx);
void flashfs_stats(uint32_t *used, uint32_t *free_bytes);
