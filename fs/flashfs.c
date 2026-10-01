/* ================================================================
 * NumWorks OS — Flash Filesystem Implementation
 * File: fs/flashfs.c
 *
 * An append-only record log plus data pool in one of two flash areas
 * (see flashfs.h for the layout and the power-loss behaviour). The
 * flash itself is reached through fs/storage.h.
 *
 * Offsets in the directory, and those handed to callers, are relative
 * to the start of the storage region.
 * ================================================================ */
#include "flashfs.h"
#include "storage.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AREA_SIZE     FFS_AREA_SIZE
#define SUPER_OFFSET  0x000U
#define LOG_OFFSET    0x100U
#define DATA_OFFSET   0x4000U
#define REC_SIZE      ((uint32_t)sizeof(ffs_entry_t))
#define LOG_RECORDS   ((DATA_OFFSET - LOG_OFFSET) / REC_SIZE)   /* 403 */
#define REC_COMMIT    ((uint32_t)offsetof(ffs_entry_t, flags))  /* programmed last */

/* Record kinds (the `flags` word). Anything else — including an erased
 * 0xFFFFFFFF left by a power cut before the commit — is ignored. */
#define REC_FILE      0x454C4946UL   /* "FILE" */
#define REC_DEL       0x204C4544UL   /* "DEL " */

_Static_assert(sizeof(ffs_entry_t) == 40, "log record must stay 40 bytes");
_Static_assert(sizeof(ffs_super_t) <= LOG_OFFSET, "superblock too large");
_Static_assert((REC_COMMIT % 4) == 0, "commit word must be word aligned");
_Static_assert(AREA_SIZE % STORAGE_ERASE_SIZE == 0, "areas must be whole erase blocks");
_Static_assert(DATA_OFFSET < AREA_SIZE, "area too small");

/* ── State ────────────────────────────────────────────────────── */
static ffs_entry_t s_dir[FFS_MAX_FILES];   /* live files, offset in region */
static uint32_t    s_count    = 0;
static uint32_t    s_log_used = 0;         /* log records consumed         */
static uint32_t    s_data_end = 0;         /* next free data, in the area  */
static uint32_t    s_area     = 0;         /* active area: 0 or 1          */
static uint32_t    s_gen      = 0;         /* its generation               */
static bool        s_present  = false;     /* storage found                */
static bool        s_mounted  = false;

/* A streamed write in progress (flashfs_stream_*): its space is
 * reserved in the active area; it only becomes a file at the end.
 * Anything that rebuilds the directory (compaction, remount) ends it. */
static struct {
    bool     active;
    char     name[FFS_NAME_LEN];
    uint32_t start, size, written;           /* start: region offset */
} s_stream;

static uint32_t align4(uint32_t v) { return (v + 3U) & ~3U; }
static uint32_t area_base(uint32_t a) { return a * AREA_SIZE; }
static const uint8_t *at(uint32_t off) { return storage_base() + off; }

static bool all_erased(const uint8_t *p, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) if (p[i] != 0xFF) return false;
    return true;
}

/* Program erased flash and check that it took. Flash can clear bits
 * but never set them, so programming anything but erased bytes would
 * corrupt them. */
static int prog(uint32_t off, const void *src, uint32_t len) {
    if (len == 0) return 0;
    const uint8_t *dst = at(off);
    if (memcmp(dst, src, len) == 0) return 0;
    if (!all_erased(dst, len)) return -1;
    if (storage_program(off, src, len) != 0) return -1;
    return memcmp(dst, src, len) == 0 ? 0 : -1;
}

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
                        uint32_t area_offset, uint32_t size, uint32_t kind) {
    memset(r, 0xFF, sizeof(*r));
    memset(r->name, 0, FFS_NAME_LEN);
    strncpy(r->name, name, FFS_NAME_LEN - 1);
    r->offset = area_offset;
    r->size   = size;
    r->flags  = kind;
}

/* ── Superblocks ──────────────────────────────────────────────── */
static ffs_super_t read_super(uint32_t a) {
    ffs_super_t sb;
    memcpy(&sb, at(area_base(a) + SUPER_OFFSET), sizeof(sb));
    return sb;
}

static bool super_valid(uint32_t a) {
    ffs_super_t sb = read_super(a);
    return sb.magic == FFS_MAGIC && sb.version == FFS_VERSION &&
           sb.generation == ~sb.generation_inv;
}

/* Everything but the magic, then the magic: until that last word is
 * programmed, the area doesn't count */
static int write_super(uint32_t a, uint32_t generation) {
    ffs_super_t sb;
    memset(&sb, 0xFF, sizeof(sb));
    sb.magic      = FFS_MAGIC;
    sb.version    = FFS_VERSION;
    sb.generation = generation;
    sb.generation_inv = ~generation;
    uint32_t base = area_base(a) + SUPER_OFFSET;
    const uint8_t *p = (const uint8_t *)&sb;
    if (prog(base + 4, p + 4, sizeof(sb) - 4) != 0) return -1;
    return prog(base, p, 4);
}

/* ── Mount: pick the newest valid area and replay its log ────── */
static int mount(void) {
    s_stream.active = false;
    s_mounted  = false;
    s_count    = 0;
    s_log_used = 0;
    s_data_end = DATA_OFFSET;

    bool v0 = super_valid(0), v1 = super_valid(1);
    if (!v0 && !v1) return FFS_ERR_FORMAT;
    s_area = (v1 && (!v0 || read_super(1).generation > read_super(0).generation)) ? 1 : 0;
    s_gen  = read_super(s_area).generation;
    uint32_t base = area_base(s_area);

    uint32_t i;
    for (i = 0; i < LOG_RECORDS; i++) {
        const uint8_t *raw = at(base + LOG_OFFSET + i * REC_SIZE);
        if (all_erased(raw, REC_SIZE)) break;          /* end of log */

        ffs_entry_t r;
        memcpy(&r, raw, sizeof(r));
        if (r.name[0] == 0 || !memchr(r.name, 0, FFS_NAME_LEN)) continue;

        if (r.flags == REC_FILE) {
            if (r.offset < DATA_OFFSET || r.offset > AREA_SIZE || (r.offset & 3U) ||
                r.size > AREA_SIZE - r.offset) continue;
            uint32_t end = r.offset + align4(r.size);
            if (end > s_data_end) s_data_end = end;
            int idx = find_entry(r.name);
            if (idx < 0) {
                if (s_count >= FFS_MAX_FILES) continue;
                idx = (int)s_count++;
            }
            set_entry(idx, r.name, base + r.offset, r.size);
        } else if (r.flags == REC_DEL) {
            int idx = find_entry(r.name);
            if (idx >= 0) remove_entry(idx);
        }
        /* Uncommitted (torn) record: skip it, but its slot stays used */
    }
    s_log_used = i;

    /* Data written for a record that never got committed still occupies
     * flash: never hand that space out again. */
    uint32_t w = AREA_SIZE;
    while (w > s_data_end && all_erased(at(base + w - 4), 4)) w -= 4;
    if (w > s_data_end) s_data_end = w;

    s_mounted = true;
    return FFS_OK;
}

/* Append one record; it only takes effect once its kind is programmed */
static int log_append(const char *name, uint32_t region_offset,
                      uint32_t size, uint32_t kind) {
    if (s_log_used >= LOG_RECORDS) return -1;
    uint32_t base = area_base(s_area);
    ffs_entry_t r;
    make_record(&r, name, region_offset - base, size, kind);
    uint32_t off = base + LOG_OFFSET + s_log_used * REC_SIZE;
    s_log_used++;   /* consumed even if programming fails */
    if (prog(off, &r, REC_COMMIT) != 0) return -1;
    return prog(off + REC_COMMIT, &kind, sizeof(kind));
}

/* ── Compaction into the other area ───────────────────────────── */
typedef enum { MOD_NONE, MOD_WRITE, MOD_DELETE, MOD_RENAME } mod_t;

typedef struct {
    mod_t       mod;
    const char *name, *new_name;
    const void *data;
    uint32_t    len;
} change_t;

/* The live files with one change applied, laid out in area `a`. With
 * write=false this only checks that they fit. */
static int lay_out(uint32_t a, const change_t *c, bool write) {
    uint32_t base = area_base(a), cur = DATA_OFFSET, nrec = 0;
    bool done = false;
    for (uint32_t i = 0; i <= s_count; i++) {
        const char *nm;
        const void *src;
        uint32_t sz;
        if (i < s_count) {
            nm  = s_dir[i].name;
            src = at(s_dir[i].offset);
            sz  = s_dir[i].size;
            if (c->mod != MOD_NONE && strncmp(nm, c->name, FFS_NAME_LEN) == 0) {
                if (c->mod == MOD_DELETE) continue;
                if (c->mod == MOD_RENAME) nm = c->new_name;
                if (c->mod == MOD_WRITE)  { src = c->data; sz = c->len; done = true; }
            }
        } else {
            if (c->mod != MOD_WRITE || done) break;   /* a new file goes last */
            nm = c->name; src = c->data; sz = c->len;
        }
        if (nrec >= LOG_RECORDS || align4(sz) > AREA_SIZE - cur) return -1;
        if (write) {
            ffs_entry_t r;
            make_record(&r, nm, cur, sz, REC_FILE);
            uint32_t roff = base + LOG_OFFSET + nrec * REC_SIZE;
            if (prog(base + cur, src, sz) != 0 ||
                prog(roff, &r, REC_COMMIT) != 0 ||
                prog(roff + REC_COMMIT, &r.flags, sizeof(r.flags)) != 0) return -1;
        }
        nrec++;
        cur += align4(sz);
    }
    return 0;
}

static int compact(const change_t *c) {
    uint32_t to = 1U - s_area;
    if (lay_out(to, c, false) != 0) return -1;          /* doesn't fit */
    /* The old area stays untouched (and valid) until the new one is
     * complete, so a failure or power cut here loses nothing. */
    int r = storage_erase(area_base(to), AREA_SIZE);
    if (r == 0) r = lay_out(to, c, true);
    if (r == 0) r = write_super(to, s_gen + 1U);
    int m = mount();
    return (r == 0 && m == FFS_OK) ? 0 : -1;
}

/* ── Public API ──────────────────────────────────────────────── */
int flashfs_init(void) {
    s_mounted = false;
    s_count   = 0;
    s_present = storage_init();
    if (!s_present) return FFS_ERR_NODEV;
    return mount();
}

int flashfs_format(void) {
    s_mounted = false;
    s_count   = 0;
    if (!s_present) return FFS_ERR_NODEV;
    /* Both areas: an old superblock left in either could outrank the
     * new one */
    if (storage_erase(area_base(1), AREA_SIZE) != 0 ||
        storage_erase(area_base(0), AREA_SIZE) != 0 ||
        write_super(0, 1) != 0) return -1;
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
    *data = (const char *)at(offset);
    return true;
}

int flashfs_read(uint32_t offset, void *buf, uint32_t len) {
    uint32_t lo = area_base(s_area) + DATA_OFFSET, hi = area_base(s_area) + AREA_SIZE;
    if (!s_mounted || offset < lo || offset > hi || len > hi - offset) return -1;
    memcpy(buf, at(offset), len);
    return (int)len;
}

int flashfs_write(const char *path, const void *data, uint32_t len) {
    if (!s_mounted || !name_ok(path)) return -1;
    if (len > FFS_MAX_FILE_SIZE) return -1;

    int idx = find_entry(path);
    if (idx < 0 && s_count >= FFS_MAX_FILES) return -1;

    uint32_t need = align4(len);
    if (s_log_used < LOG_RECORDS && need <= AREA_SIZE - s_data_end) {
        /* Append the data, then the record that points at it */
        uint32_t off = area_base(s_area) + s_data_end;
        s_data_end += need;   /* consumed even if programming fails */
        if (prog(off, data, len) != 0 || log_append(path, off, len, REC_FILE) != 0)
            return -1;
        if (idx < 0) idx = (int)s_count++;
        set_entry(idx, path, off, len);
        return (int)len;
    }

    /* Log or data pool full: compact, with the new contents in place */
    change_t c = { MOD_WRITE, path, NULL, data, len };
    return compact(&c) == 0 ? (int)len : -1;
}

int flashfs_delete(const char *path) {
    if (!s_mounted) return -1;
    int idx = find_entry(path);
    if (idx < 0) return -1;

    if (s_log_used < LOG_RECORDS) {
        if (log_append(path, area_base(s_area) + DATA_OFFSET, 0, REC_DEL) != 0) return -1;
        remove_entry(idx);
        return 0;
    }
    change_t c = { MOD_DELETE, path, NULL, NULL, 0 };
    return compact(&c);
}

int flashfs_rename(const char *from, const char *to) {
    if (!s_mounted || !name_ok(to)) return -1;
    int idx = find_entry(from);
    if (idx < 0) return -1;
    if (find_entry(to) >= 0) return -1;   /* don't silently replace a file */

    if (s_log_used + 2 <= LOG_RECORDS) {
        /* New name first: a power cut in between leaves both names,
         * never neither. */
        int r = log_append(to, s_dir[idx].offset, s_dir[idx].size, REC_FILE);
        if (r == 0) r = log_append(from, area_base(s_area) + DATA_OFFSET, 0, REC_DEL);
        if (r != 0) { mount(); return -1; }   /* resync with what's on flash */
        set_entry(idx, to, s_dir[idx].offset, s_dir[idx].size);
        return 0;
    }
    change_t c = { MOD_RENAME, from, to, NULL, 0 };
    return compact(&c);
}

/* ── Streamed writes ─────────────────────────────────────────────
 * For files too big to hold in RAM: the data is programmed as it
 * arrives, into space reserved up front, and the file only changes
 * when flashfs_stream_end() appends its record. A power cut or an
 * abort before that leaves the old file (if any) as it was. */
int flashfs_stream_begin(const char *path, uint32_t size) {
    if (s_stream.active) return -1;      /* one at a time; the other one goes on */
    if (!s_mounted || !name_ok(path) || size > FFS_MAX_FILE_SIZE) return -1;
    if (find_entry(path) < 0 && s_count >= FFS_MAX_FILES) return -1;
    uint32_t need = align4(size);
    if (s_log_used >= LOG_RECORDS || need > AREA_SIZE - s_data_end) {
        change_t none = { MOD_NONE, NULL, NULL, NULL, 0 };    /* make room */
        if (compact(&none) != 0) return -1;
        if (s_log_used >= LOG_RECORDS || need > AREA_SIZE - s_data_end) return -1;
    }
    strncpy(s_stream.name, path, FFS_NAME_LEN - 1);
    s_stream.name[FFS_NAME_LEN - 1] = 0;
    s_stream.start   = area_base(s_area) + s_data_end;
    s_stream.size    = size;
    s_stream.written = 0;
    s_data_end += need;          /* other writes go after the reservation */
    s_stream.active = true;
    return 0;
}

int flashfs_stream_write(const void *data, uint32_t len) {
    if (!s_stream.active || len > s_stream.size - s_stream.written) return -1;
    if (prog(s_stream.start + s_stream.written, data, len) != 0) {
        s_stream.active = false;
        return -1;
    }
    s_stream.written += len;
    return (int)len;
}

int flashfs_stream_end(void) {
    if (!s_stream.active || s_stream.written != s_stream.size) {
        s_stream.active = false;
        return -1;
    }
    s_stream.active = false;
    const char *name = s_stream.name;
    if (find_entry(name) < 0 && s_count >= FFS_MAX_FILES) return -1;
    if (s_log_used < LOG_RECORDS) {
        if (log_append(name, s_stream.start, s_stream.size, REC_FILE) != 0) return -1;
        int idx = find_entry(name);
        if (idx < 0) idx = (int)s_count++;
        set_entry(idx, name, s_stream.start, s_stream.size);
        return (int)s_stream.size;
    }
    /* The log filled up meanwhile: compact, copying the streamed data
     * from where it was programmed */
    char nm[FFS_NAME_LEN];
    memcpy(nm, name, FFS_NAME_LEN);
    change_t c = { MOD_WRITE, nm, NULL, at(s_stream.start), s_stream.size };
    return compact(&c) == 0 ? (int)c.len : -1;
}

void flashfs_stream_abort(void) {
    s_stream.active = false;     /* the reserved space is reclaimed by compaction */
}

bool flashfs_stream_active(void) {
    return s_stream.active;
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
    if (free_bytes) *free_bytes = (AREA_SIZE - DATA_OFFSET) - u;
}
