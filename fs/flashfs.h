/* ================================================================
 * NumWorks OS — Flash Filesystem
 * File: fs/flashfs.h
 *
 * Stored in two equal areas of flash (fs/storage.h), of which one is
 * active. Layout of an area:
 *
 *  [0x0000..0x00FF]  Superblock: magic, version, generation
 *  [0x0100..0x3FFF]  Record log: 403 x 40-byte records, append-only
 *  [0x4000..end]     File data pool, append-only
 *
 * Flash can only be programmed from the erased state, so nothing is
 * ever rewritten in place. Writing, deleting or renaming a file appends
 * a record to the log, and the newest record for a name wins. A record
 * only counts once its last word (the kind) is programmed, so a power
 * cut mid-write leaves the previous version of the file intact.
 *
 * When the log or the data pool is full, the live files are copied
 * into the other area, which gets the next generation number; the
 * superblock's magic is programmed last. At mount the valid area with
 * the highest generation wins, so a power cut during compaction leaves
 * the old area in use, with every file in it.
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../include/config.h"

#define FFS_MAGIC   0x4E574F53UL  /* "NWOS" */
#define FFS_VERSION 3             /* 3 = two areas with a generation number */

/* flashfs_init() / flashfs_format() results */
#define FFS_OK          0
#define FFS_ERR_FORMAT (-1)   /* no valid filesystem: format it */
#define FFS_ERR_NODEV  (-2)   /* no usable flash (storage_status() says why) */

/* Directory entry, also the on-flash log record (40 bytes).
 * Entries handed out by flashfs_ls() have flags bit 0 set and an
 * `offset` that can be passed to flashfs_read(). */
typedef struct __attribute__((packed)) {
    char     name[FFS_NAME_LEN];  /* Null-terminated filename    */
    uint32_t offset;              /* Data offset                 */
    uint32_t size;                /* File size in bytes          */
    uint32_t flags;               /* Bit 0 = valid / record kind */
    uint32_t _pad;
} ffs_entry_t;

/* Superblock. The magic is programmed last: it commits the area.
 * The generation is stored twice, the second time inverted: an
 * interrupted erase only turns bits to 1, so it can't produce a
 * matching pair (or make an old area look newer). */
typedef struct __attribute__((packed)) {
    uint32_t   magic;
    uint8_t    version;
    uint8_t    _pad[3];
    uint32_t   generation;
    uint32_t   generation_inv;    /* ~generation */
} ffs_super_t;

/* Public API */
int  flashfs_init(void);     /* FFS_OK, FFS_ERR_FORMAT or FFS_ERR_NODEV */
int  flashfs_format(void);

int  flashfs_open_read(const char *path, uint32_t *offset, uint32_t *size);
int  flashfs_read(uint32_t offset, void *buf, uint32_t len);
/* A file's contents in place (the flash is memory-mapped). Valid until
 * the next write, delete or rename, which may compact the files. */
bool flashfs_map(const char *path, const char **data, uint32_t *size);
int  flashfs_write(const char *path, const void *data, uint32_t len);
int  flashfs_delete(const char *path);
int  flashfs_rename(const char *from, const char *to);
bool flashfs_exists(const char *path);
bool flashfs_mounted(void);

/* Streamed write, for files too big for RAM: begin reserves space for
 * `size` bytes (fails if they don't fit, even after compacting), write
 * adds the data in order, end commits it once all `size` bytes are in.
 * Until then the old file stays as it was. One stream at a time (begin
 * fails while another is open); a compaction caused by another write
 * ends it (write/end then fail). */
int  flashfs_stream_begin(const char *path, uint32_t size);
int  flashfs_stream_write(const void *data, uint32_t len);
int  flashfs_stream_end(void);
void flashfs_stream_abort(void);
bool flashfs_stream_active(void);

int  flashfs_ls(void (*cb)(const ffs_entry_t *e, void *ctx), void *ctx);
void flashfs_stats(uint32_t *used, uint32_t *free_bytes);
