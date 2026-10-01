/* ================================================================
 * NumWorks OS — Internal Flash Filesystem Implementation
 * File: fs/flashfs.c
 *
 * Uses STM32F730 sector 7 (64 KB @ 0x0807_0000), as an append-only
 * record log plus data pool. See flashfs.h for the layout and the
 * power-loss behaviour.
 *
 * Code size target: < 6 KB
 * ================================================================ */
#include "flashfs.h"
#include "../include/stm32f730.h"
#include "../include/config.h"
#include "../include/string.h"
#include <stdint.h>

#define SUPER_OFFSET  0x000U
#define LOG_OFFSET    0x100U
#define DATA_OFFSET   0x1000U
#define REC_SIZE      ((uint32_t)sizeof(ffs_entry_t))
#define LOG_RECORDS   ((DATA_OFFSET - LOG_OFFSET) / REC_SIZE)   /* 96 */
#define REC_COMMIT    ((uint32_t)offsetof(ffs_entry_t, flags))  /* programmed last */
#define FS_END        (FFS_START + FFS_SIZE)

/* Record kinds (the `flags` word). Anything else — including an erased
 * 0xFFFFFFFF left by a power cut before the commit — is ignored. */
#define REC_FILE      0x454C4946UL   /* "FILE" */
#define REC_DEL       0x204C4544UL   /* "DEL " */

_Static_assert(sizeof(ffs_entry_t) == 40, "log record must stay 40 bytes");
_Static_assert(sizeof(ffs_super_t) <= LOG_OFFSET, "superblock too large");
_Static_assert((REC_COMMIT % 4) == 0, "commit word must be word aligned");

/* ── Flash sector operations ─────────────────────────────────── */
static void flash_unlock(void) {
    if (FLASH_R->CR & FLASH_CR_LOCK) {
        FLASH_R->KEYR = FLASH_KEY1;
        FLASH_R->KEYR = FLASH_KEY2;
    }
}
static void flash_lock(void) {
    FLASH_R->CR |= FLASH_CR_LOCK;
}

/* Wait for the operation to finish; report and clear error flags */
static int flash_wait(void) {
    __asm volatile("dsb" ::: "memory");
    while (FLASH_R->SR & FLASH_SR_BSY) {}
    uint32_t err = FLASH_R->SR & FLASH_SR_ERRORS;
    FLASH_R->SR = err | FLASH_SR_EOP;   /* write 1 to clear */
    return err ? -1 : 0;
}

static bool all_erased(const uint8_t *p, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) if (p[i] != 0xFF) return false;
    return true;
}

static int flash_erase_sector(uint8_t sector) {
    FLASH_R->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
    FLASH_R->CR  = FLASH_CR_SER | FLASH_CR_PSIZE_32 | FLASH_CR_SNB(sector);
    FLASH_R->CR |= FLASH_CR_STRT;
    int r = flash_wait();
    FLASH_R->CR  = 0;
    if (r == 0 && !all_erased((const uint8_t *)FFS_START, FFS_SIZE)) r = -1;
    return r;
}

/* Program one word. Only erased words may be programmed: flash can
 * clear bits but never set them, so anything else would corrupt it. */
static int flash_program_word(uint32_t addr, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)addr;
    if (*p == val) return 0;
    if (*p != 0xFFFFFFFFUL) return -1;
    FLASH_R->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
    FLASH_R->CR = FLASH_CR_PG | FLASH_CR_PSIZE_32;
    *p = val;
    int r = flash_wait();
    FLASH_R->CR = 0;
    return (r == 0 && *p == val) ? 0 : -1;
}

/* Program a byte range starting at a word boundary; pads with 0xFF */
static int flash_program(uint32_t addr, const void *buf, uint32_t len) {
    const uint8_t *src = (const uint8_t *)buf;
    while (len) {
        uint32_t w = 0xFFFFFFFFUL;
        uint32_t n = len < 4 ? len : 4;
        memcpy(&w, src, n);
        if (flash_program_word(addr, w) != 0) return -1;
        src += n; addr += 4; len -= n;
    }
    return 0;
}

/* FFS_START must be real flash on this part (a 64 KB STM32F730x8 has
 * no sector 7): reading or erasing past the end of flash faults. */
static bool flash_present(void) {
    uint32_t flash_end = FLASH_BASE_ADDR + (uint32_t)FLASH_SIZE_KB * 1024U;
    return FFS_START >= FLASH_BASE_ADDR && FS_END <= flash_end;
}

/* ── In-RAM directory of live files ──────────────────────────── */
static ffs_entry_t s_dir[FFS_MAX_FILES];   /* offset = absolute address */
static uint32_t    s_count    = 0;
static uint32_t    s_log_used = 0;         /* log records consumed       */
static uint32_t    s_data_end = 0;         /* next free data address     */
static bool        s_mounted  = false;

static uint8_t    *s_scratch     = NULL;
static uint32_t    s_scratch_len = 0;
static void      (*s_scratch_released)(void) = NULL;

/* Pointer to flash-mapped FS region */
static const uint8_t *FS = (const uint8_t *)FFS_START;

static uint32_t align4(uint32_t v) { return (v + 3U) & ~3U; }

static bool name_ok(const char *name) {
    size_t n = strnlen(name, FFS_NAME_LEN);
    return n > 0 && n < FFS_NAME_LEN;   /* reject rather than truncate */
}

static int find_entry(const char *name) {
    for (uint32_t i = 0; i < s_count; i++) {
        if (strncmp(s_dir[i].name, name, FFS_NAME_LEN) == 0)
            return (int)i;
    }
    return -1;
}

static void remove_entry(int idx) {
    memmove(&s_dir[idx], &s_dir[idx + 1],
            (s_count - (uint32_t)idx - 1) * sizeof(ffs_entry_t));
    s_count--;
}

static void set_entry(int idx, const char *name, uint32_t offset, uint32_t size) {
    memset(&s_dir[idx], 0, sizeof(ffs_entry_t));
    strncpy(s_dir[idx].name, name, FFS_NAME_LEN - 1);
    s_dir[idx].offset = offset;
    s_dir[idx].size   = size;
    s_dir[idx].flags  = 1;
}

static void make_record(ffs_entry_t *r, const char *name,
                        uint32_t rel_offset, uint32_t size, uint32_t kind) {
    memset(r, 0xFF, sizeof(*r));
    memset(r->name, 0, FFS_NAME_LEN);
    strncpy(r->name, name, FFS_NAME_LEN - 1);
    r->offset = rel_offset;
    r->size   = size;
    r->flags  = kind;
}

/* Rebuild the RAM directory by replaying the log */
static int mount(void) {
    s_mounted  = false;
    s_count    = 0;
    s_log_used = 0;
    s_data_end = FFS_START + DATA_OFFSET;

    const ffs_super_t *sb = (const ffs_super_t *)(FS + SUPER_OFFSET);
    if (sb->magic != FFS_MAGIC || sb->version != FFS_VERSION)
        return FFS_ERR_FORMAT;

    uint32_t i;
    for (i = 0; i < LOG_RECORDS; i++) {
        const uint8_t *raw = FS + LOG_OFFSET + i * REC_SIZE;
        if (all_erased(raw, REC_SIZE)) break;          /* end of log */

        ffs_entry_t r;
        memcpy(&r, raw, sizeof(r));
        if (r.name[0] == 0 || !memchr(r.name, 0, FFS_NAME_LEN)) continue;

        if (r.flags == REC_FILE) {
            if (r.offset < DATA_OFFSET || r.offset > FFS_SIZE || (r.offset & 3U) ||
                r.size > FFS_SIZE - r.offset) continue;
            uint32_t end = FFS_START + r.offset + align4(r.size);
            if (end > s_data_end) s_data_end = end;
            int idx = find_entry(r.name);
            if (idx < 0) {
                if (s_count >= FFS_MAX_FILES) continue;
                idx = (int)s_count++;
            }
            set_entry(idx, r.name, FFS_START + r.offset, r.size);
        } else if (r.flags == REC_DEL) {
            int idx = find_entry(r.name);
            if (idx >= 0) remove_entry(idx);
        }
        /* Uncommitted (torn) record: skip it, but its slot stays used */
    }
    s_log_used = i;

    /* Data written for a record that never got committed still occupies
     * flash: never hand that space out again. */
    uint32_t w = FS_END;
    while (w > s_data_end && *(const uint32_t *)(w - 4) == 0xFFFFFFFFUL) w -= 4;
    if (w > s_data_end) s_data_end = w;

    s_mounted = true;
    return FFS_OK;
}

/* Append one record; it only takes effect once its kind is programmed */
static int log_append(const char *name, uint32_t abs_offset,
                      uint32_t size, uint32_t kind) {
    if (s_log_used >= LOG_RECORDS) return -1;
    ffs_entry_t r;
    make_record(&r, name, abs_offset - FFS_START, size, kind);
    uint32_t addr = FFS_START + LOG_OFFSET + s_log_used * REC_SIZE;
    s_log_used++;   /* consumed even if programming fails */
    if (flash_program(addr, &r, REC_COMMIT) != 0) return -1;
    return flash_program_word(addr + REC_COMMIT, kind);
}

/* ── Compaction ──────────────────────────────────────────────── */
typedef enum { MOD_WRITE, MOD_DELETE, MOD_RENAME } mod_t;

/* Write the live files, with one change applied, into a fresh sector */
static int compact(mod_t mod, const char *name, const void *data,
                   uint32_t len, const char *new_name) {
    if (!s_scratch || s_scratch_len < FFS_SIZE) return -1;

    /* Stage the whole image in RAM first: once the sector is erased,
     * the old file contents are gone. */
    uint8_t *img = s_scratch;
    memset(img, 0xFF, FFS_SIZE);
    uint32_t cur = DATA_OFFSET, nrec = 0;
    bool done = false, fits = true;

    for (uint32_t i = 0; i <= s_count && fits; i++) {
        const char *nm;
        const void *src;
        uint32_t sz;
        if (i < s_count) {
            nm  = s_dir[i].name;
            src = (const void *)s_dir[i].offset;
            sz  = s_dir[i].size;
            if (strncmp(nm, name, FFS_NAME_LEN) == 0) {
                if (mod == MOD_DELETE) continue;
                if (mod == MOD_RENAME) nm = new_name;
                if (mod == MOD_WRITE)  { src = data; sz = len; done = true; }
            }
        } else {
            if (mod != MOD_WRITE || done) break;   /* new file goes last */
            nm = name; src = data; sz = len;
        }
        if (nrec >= LOG_RECORDS || cur > FFS_SIZE || align4(sz) > FFS_SIZE - cur) {
            fits = false;
            break;
        }
        if (sz) memcpy(img + cur, src, sz);
        ffs_entry_t r;
        make_record(&r, nm, cur, sz, REC_FILE);
        memcpy(img + LOG_OFFSET + nrec * REC_SIZE, &r, sizeof(r));
        nrec++;
        cur += align4(sz);
    }

    int r = fits ? 0 : -1;
    if (r == 0) {
        flash_unlock();
        r = flash_erase_sector(FFS_SECTOR_NUM);
        /* Data, then records (each committed by its kind word), and the
         * superblock last: an interrupted compaction reads back as
         * "needs formatting", never as a half-valid filesystem. */
        if (r == 0) r = flash_program(FFS_START + DATA_OFFSET,
                                      img + DATA_OFFSET, cur - DATA_OFFSET);
        for (uint32_t j = 0; r == 0 && j < nrec; j++) {
            uint32_t off = LOG_OFFSET + j * REC_SIZE;
            r = flash_program(FFS_START + off, img + off, REC_COMMIT);
            if (r == 0) r = flash_program_word(FFS_START + off + REC_COMMIT, REC_FILE);
        }
        if (r == 0) {
            ffs_super_t sb;
            memset(&sb, 0xFF, sizeof(sb));
            sb.magic   = FFS_MAGIC;
            sb.version = FFS_VERSION;
            r = flash_program(FFS_START + SUPER_OFFSET, &sb, sizeof(sb));
        }
        flash_lock();
    }

    if (s_scratch_released) s_scratch_released();
    int m = mount();
    return (r == 0 && m == FFS_OK) ? 0 : -1;
}

/* ── Public API ──────────────────────────────────────────────── */
void flashfs_set_scratch(void *buf, uint32_t len, void (*released)(void)) {
    s_scratch          = (uint8_t *)buf;
    s_scratch_len      = len;
    s_scratch_released = released;
}

int flashfs_init(void) {
    s_mounted = false;
    s_count   = 0;
    if (!flash_present()) return FFS_ERR_NODEV;
    return mount();
}

int flashfs_format(void) {
    s_mounted = false;
    s_count   = 0;
    if (!flash_present()) return FFS_ERR_NODEV;

    ffs_super_t sb;
    memset(&sb, 0xFF, sizeof(sb));
    sb.magic   = FFS_MAGIC;
    sb.version = FFS_VERSION;

    flash_unlock();
    int r = flash_erase_sector(FFS_SECTOR_NUM);
    if (r == 0) r = flash_program(FFS_START + SUPER_OFFSET, &sb, sizeof(sb));
    flash_lock();
    if (r != 0) return -1;
    return mount();
}

int flashfs_open_read(const char *path, uint32_t *offset, uint32_t *size) {
    int idx = find_entry(path);
    if (idx < 0) return -1;
    *offset = s_dir[idx].offset;
    *size   = s_dir[idx].size;
    return 0;
}

bool flashfs_map(const char *path, const char **data, uint32_t *size) {
    uint32_t offset;
    if (flashfs_open_read(path, &offset, size) != 0) return false;
    *data = (const char *)(uintptr_t)offset;
    return true;
}

int flashfs_read(uint32_t offset, void *buf, uint32_t len) {
    if (offset < FFS_START + DATA_OFFSET || offset > FS_END ||
        len > FS_END - offset) return -1;
    memcpy(buf, (const void *)offset, len);
    return (int)len;
}

int flashfs_write(const char *path, const void *data, uint32_t len) {
    if (!s_mounted || !name_ok(path)) return -1;
    if (len > FFS_MAX_FILE_SIZE) return -1;

    int idx = find_entry(path);
    if (idx < 0 && s_count >= FFS_MAX_FILES) return -1;

    uint32_t need = align4(len);
    if (s_log_used < LOG_RECORDS && need <= FS_END - s_data_end) {
        /* Append the data, then the record that points at it */
        uint32_t off = s_data_end;
        s_data_end += need;   /* consumed even if programming fails */
        flash_unlock();
        int r = flash_program(off, data, len);
        if (r == 0) r = log_append(path, off, len, REC_FILE);
        flash_lock();
        if (r != 0) return -1;
        if (idx < 0) idx = (int)s_count++;
        set_entry(idx, path, off, len);
        return (int)len;
    }

    /* Log or data pool full: compact, with the new contents in place */
    return compact(MOD_WRITE, path, data, len, NULL) == 0 ? (int)len : -1;
}

int flashfs_delete(const char *path) {
    if (!s_mounted) return -1;
    int idx = find_entry(path);
    if (idx < 0) return -1;

    if (s_log_used < LOG_RECORDS) {
        flash_unlock();
        int r = log_append(path, FFS_START + DATA_OFFSET, 0, REC_DEL);
        flash_lock();
        if (r != 0) return -1;
        remove_entry(idx);
        return 0;
    }
    return compact(MOD_DELETE, path, NULL, 0, NULL);
}

int flashfs_rename(const char *from, const char *to) {
    if (!s_mounted || !name_ok(to)) return -1;
    int idx = find_entry(from);
    if (idx < 0) return -1;
    if (find_entry(to) >= 0) return -1;   /* don't silently replace a file */

    if (s_log_used + 2 <= LOG_RECORDS) {
        /* New name first: a power cut in between leaves both names,
         * never neither. */
        flash_unlock();
        int r = log_append(to, s_dir[idx].offset, s_dir[idx].size, REC_FILE);
        if (r == 0) r = log_append(from, FFS_START + DATA_OFFSET, 0, REC_DEL);
        flash_lock();
        if (r != 0) { mount(); return -1; }   /* resync with what's on flash */
        set_entry(idx, to, s_dir[idx].offset, s_dir[idx].size);
        return 0;
    }
    return compact(MOD_RENAME, from, NULL, 0, to);
}

bool flashfs_mounted(void) {
    return s_mounted;
}

bool flashfs_exists(const char *path) {
    return find_entry(path) >= 0;
}

int flashfs_ls(void (*cb)(const ffs_entry_t *e, void *ctx), void *ctx) {
    for (uint32_t i = 0; i < s_count; i++) cb(&s_dir[i], ctx);
    return (int)s_count;
}

void flashfs_stats(uint32_t *used, uint32_t *free_bytes) {
    uint32_t u = 0;
    for (uint32_t i = 0; i < s_count; i++) u += align4(s_dir[i].size);
    if (used)       *used       = u;
    if (free_bytes) *free_bytes = (FFS_SIZE - DATA_OFFSET) - u;
}
