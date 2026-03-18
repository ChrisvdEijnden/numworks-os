/* ============================================================
 * NumWorks OS — Filesystem API
 * Two volumes:
 *   "0:" — internal flash filesystem (custom log-structured)
 *   "1:" — external (reserved for future SD/USB expansion)
 * ============================================================ */
#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/config.h"

/* ── Error codes ──────────────────────────────────────────── */
typedef enum {
    FS_OK       =  0,
    FS_ERR      = -1,   /* generic error                     */
    FS_NOENT    = -2,   /* no such file or directory         */
    FS_EXIST    = -3,   /* file already exists               */
    FS_NOSPC    = -4,   /* no space left                     */
    FS_NOTDIR   = -5,   /* not a directory                   */
    FS_ISDIR    = -6,   /* is a directory                    */
    FS_INVAL    = -7,   /* invalid argument                  */
    FS_NOTEMPTY = -8,   /* directory not empty               */
    FS_TOOLONG  = -9,   /* path too long                     */
    FS_RONLY    = -10,  /* read-only                         */
} fs_err_t;

/* ── File open flags ──────────────────────────────────────── */
#define FS_O_RDONLY   0x01
#define FS_O_WRONLY   0x02
#define FS_O_RDWR     0x03
#define FS_O_CREAT    0x10
#define FS_O_TRUNC    0x20
#define FS_O_APPEND   0x40

/* ── Stat structure ───────────────────────────────────────── */
#define FS_NAME_MAX  32
typedef struct {
    char     name[FS_NAME_MAX];
    uint32_t size;
    uint32_t mtime;    /* seconds since 2000-01-01            */
    uint8_t  is_dir;
} fs_stat_t;

/* ── File handle (opaque) ─────────────────────────────────── */
typedef struct fs_file fs_file_t;

/* ── Directory handle ─────────────────────────────────────── */
typedef struct fs_dir  fs_dir_t;

/* ── Filesystem init ──────────────────────────────────────── */
fs_err_t fs_init(void);
fs_err_t fs_format(void);    /* erase + reinitialise flash FS  */
void     fs_stats(uint32_t *total, uint32_t *used);

/* ── File operations ──────────────────────────────────────── */
fs_file_t *fs_open (const char *path, int flags);
int        fs_read (fs_file_t *f, void *buf, uint32_t len);
int        fs_write(fs_file_t *f, const void *buf, uint32_t len);
int        fs_seek (fs_file_t *f, int32_t offset, int whence);
int        fs_tell (fs_file_t *f);
int        fs_size (fs_file_t *f);
void       fs_close(fs_file_t *f);
fs_err_t   fs_unlink(const char *path);
fs_err_t   fs_rename(const char *old, const char *newpath);

/* ── Directory operations ─────────────────────────────────── */
fs_err_t   fs_mkdir(const char *path);
fs_err_t   fs_rmdir(const char *path);
fs_dir_t  *fs_opendir(const char *path);
int        fs_readdir(fs_dir_t *d, fs_stat_t *st); /* 1=ok 0=end -1=err */
void       fs_closedir(fs_dir_t *d);

/* ── Stat ─────────────────────────────────────────────────── */
fs_err_t   fs_stat(const char *path, fs_stat_t *st);

/* ── Seek whence ──────────────────────────────────────────── */
#define FS_SEEK_SET 0
#define FS_SEEK_CUR 1
#define FS_SEEK_END 2

/* ── Working directory ────────────────────────────────────── */
void       fs_getcwd(char *buf, uint32_t size);
fs_err_t   fs_chdir(const char *path);

#endif /* FS_H */
