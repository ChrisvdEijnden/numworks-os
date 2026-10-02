#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <setjmp.h>
#include "../../fs/flashfs.h"
extern uint8_t sim_mem[]; extern int sim_present, sim_protected; extern long sim_ops, sim_cut_at; extern jmp_buf sim_cut;
static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)
static int content_is(const char *name, const char *want, uint32_t wlen) {
    uint32_t off, sz; static char buf[110000];
    if (flashfs_open_read(name, &off, &sz) < 0) return 0;
    if (flashfs_read(off, buf, sz) != (int)sz) return 0;
    return sz == wlen && memcmp(buf, want, sz) == 0;
}
#define IS(name, s) content_is(name, s, (uint32_t)strlen(s))
static void count_cb(const ffs_entry_t *e, void *c) { (void)e; (*(int *)c)++; }
static int nfiles(void) { int n = 0; flashfs_ls(count_cb, &n); return n; }
static int reboot(void) { return flashfs_init(); }
static uint32_t gen(int a) { uint32_t g; memcpy(&g, sim_mem + a * FFS_AREA_SIZE + 8, 4); return g; }
static uint8_t snap[STORAGE_SIZE];
static int valid(int a) { uint32_t m; memcpy(&m, sim_mem + a * FFS_AREA_SIZE, 4); return m == FFS_MAGIC; }

int main(void) {
    char buf[64];
    puts("no storage:");
    sim_present = 0;
    CHECK(flashfs_init() == FFS_ERR_NODEV, "init reports FFS_ERR_NODEV");
    CHECK(flashfs_write("x.txt", "x", 1) < 0 && flashfs_format() == FFS_ERR_NODEV, "writes and format fail cleanly");
    sim_present = 1;

    puts("format + basic persistence:");
    memset(sim_mem, 0xFF, STORAGE_SIZE);
    CHECK(flashfs_init() == FFS_ERR_FORMAT, "erased storage -> FFS_ERR_FORMAT (no silent format)");
    CHECK(flashfs_format() == FFS_OK, "format");
    flashfs_write("a.txt", "hello", 5);
    flashfs_write("b.txt", "world!", 6);
    flashfs_write("a.txt", "HELLO2", 6);
    reboot();
    CHECK(IS("a.txt", "HELLO2") && IS("b.txt", "world!"), "updated a.txt and b.txt survive reboot");
    const char *p; uint32_t n;
    CHECK(flashfs_map("b.txt", &p, &n) && n == 6 && !memcmp(p, "world!", 6), "flashfs_map gives the contents in place");

    puts("delete / rename:");
    CHECK(flashfs_delete("b.txt") == 0 && flashfs_rename("a.txt", "c.txt") == 0, "delete b.txt, rename a.txt -> c.txt");
    flashfs_write("d.txt", "", 0);
    CHECK(flashfs_rename("c.txt", "d.txt") < 0, "rename onto an existing file refused");
    reboot();
    CHECK(!flashfs_exists("b.txt") && !flashfs_exists("a.txt") && IS("c.txt", "HELLO2") && IS("d.txt", ""), "state survives reboot");

    puts("log full -> compaction into the other area:");
    int ok = 1; uint32_t g0 = gen(0);
    for (int i = 0; i < 1000; i++) { snprintf(buf, sizeof buf, "v%d", i); if (flashfs_write("log.txt", buf, strlen(buf)) < 0) ok = 0; }
    CHECK(ok, "1000 updates of one file all succeed");
    CHECK(valid(0) && valid(1) && (gen(0) > g0 || gen(1) > g0), "both areas used, generation went up");
    reboot();
    CHECK(IS("log.txt", "v999") && IS("c.txt", "HELLO2"), "latest version + other files intact after reboot");

    puts("data pool full:");
    static char big[8192]; memset(big, 'Z', sizeof big);
    int written = 0;
    for (int i = 0; i < 20; i++) { snprintf(buf, sizeof buf, "big%d.bin", i); if (flashfs_write(buf, big, sizeof big) == (int)sizeof big) written++; }
    printf("       (%d x 8 KB files fit)\n", written);
    CHECK(written >= 12 && written < 20, "fills up, then refuses further writes");
    reboot();
    CHECK(IS("c.txt", "HELLO2") && IS("log.txt", "v999") && content_is("big0.bin", big, sizeof big), "existing files intact after hitting full");
    for (int i = 0; i < 20; i++) { snprintf(buf, sizeof buf, "big%d.bin", i); flashfs_delete(buf); }
    CHECK(flashfs_write("after.txt", "space reclaimed", 15) == 15, "space reclaimed after deletes");

    puts("validation:");
    CHECK(flashfs_write("this_name_is_way_too_long.txt", "x", 1) < 0, "over-long name rejected (not truncated)");
    CHECK(flashfs_read(0, buf, 4) < 0 && flashfs_read(STORAGE_SIZE - 8, buf, 64) < 0, "out-of-range reads rejected");
    char many[40][16]; int made = 0;
    for (int i = 0; i < 40; i++) { snprintf(many[i], 16, "f%d", i); if (flashfs_write(many[i], "x", 1) == 1) made++; }
    CHECK(nfiles() == FFS_MAX_FILES, "file count stops at FFS_MAX_FILES");
    for (int i = 0; i < 40; i++) flashfs_delete(many[i]);

    puts("write protection (writes silently ignored by the flash):");
    sim_protected = 1;
    CHECK(flashfs_write("prot.txt", "data", 4) < 0, "write reported as failed, not as success");
    sim_protected = 0;
    reboot();
    CHECK(!flashfs_exists("prot.txt") && IS("c.txt", "HELLO2"), "nothing half-written after reboot");

    puts("power cut at every step of an append:");
    reboot();
    memcpy(snap, sim_mem, STORAGE_SIZE);
    int lost = 0, steps = 0;
    for (long k = 1; ; k++) {
        memcpy(sim_mem, snap, STORAGE_SIZE); reboot();
        sim_ops = 0; sim_cut_at = k;
        int cut = setjmp(sim_cut);
        if (!cut) { flashfs_write("c.txt", "NEW-C", 5); sim_cut_at = -1; }
        sim_cut_at = -1;
        reboot();
        int old = IS("c.txt", "HELLO2"), nw = IS("c.txt", "NEW-C");
        if (!(old || nw) || !IS("log.txt", "v999")) lost++;
        if (!cut) { steps = (int)k; break; }
    }
    printf("       (%d steps)\n", steps);
    CHECK(lost == 0, "after any cut: c.txt is the old or the new version, others intact");

    puts("power cut at every step of a compaction:");
    memcpy(sim_mem, snap, STORAGE_SIZE); reboot();
    /* fill the log so the next write compacts */
    for (int i = 0; ; i++) {
        uint32_t before = valid(0) + valid(1) ? gen(0) + gen(1) : 0;
        snprintf(buf, sizeof buf, "w%d", i);
        memcpy(snap, sim_mem, STORAGE_SIZE);
        flashfs_write("log.txt", buf, strlen(buf));
        if (gen(0) + gen(1) != before) { memcpy(sim_mem, snap, STORAGE_SIZE); break; }   /* that one compacted: undo */
    }
    reboot();
    char last[16]; { uint32_t o, s; flashfs_open_read("log.txt", &o, &s); flashfs_read(o, last, s); last[s] = 0; }
    memcpy(snap, sim_mem, STORAGE_SIZE);
    lost = 0; steps = 0; int unusable = 0;
    for (long k = 1; ; k++) {
        memcpy(sim_mem, snap, STORAGE_SIZE); reboot();
        sim_ops = 0; sim_cut_at = k;
        int cut = setjmp(sim_cut);
        if (!cut) { flashfs_write("log.txt", "COMPACTED", 9); }
        sim_cut_at = -1;
        int r = reboot();
        if (r != FFS_OK) { unusable++; }
        else {
            int ok2 = (IS("log.txt", last) || IS("log.txt", "COMPACTED")) && IS("c.txt", "HELLO2") && IS("after.txt", "space reclaimed");
            if (!ok2) lost++;
            if (flashfs_write("probe.txt", "p", 1) != 1) unusable++;
        }
        if (!cut) { steps = (int)k; break; }
    }
    printf("       (%d steps)\n", steps);
    CHECK(steps > 30, "compaction has many program/erase steps (each one cut)");
    CHECK(unusable == 0, "after any cut the filesystem mounts and accepts writes");
    CHECK(lost == 0, "after any cut every file is intact (old or new version)");

    puts("format:");
    reboot();
    uint32_t g = gen(0) > gen(1) ? gen(0) : gen(1);
    CHECK(g > 1, "generation advanced by compactions");
    CHECK(flashfs_format() == FFS_OK && nfiles() == 0, "format gives an empty filesystem");
    reboot();
    CHECK(nfiles() == 0 && !(valid(0) && valid(1)), "old area doesn't come back after format");

    puts("torn record:");
    flashfs_write("t.txt", "keep", 4);
    reboot();
    uint8_t *log = sim_mem + (valid(1) && gen(1) > gen(0) ? FFS_AREA_SIZE : 0) + 0x100; int slot = 0;
    while (slot < 403) { int e = 1; for (int k = 0; k < 40; k++) if (log[slot*40+k] != 0xFF) e = 0; if (e) break; slot++; }
    memset(log + slot*40, 0, 24); strcpy((char*)log + slot*40, "t.txt"); uint32_t z = 0x4000; memcpy(log + slot*40 + 24, &z, 4);
    reboot();
    CHECK(IS("t.txt", "keep") && flashfs_write("u.txt", "fine", 4) == 4, "torn record ignored, writes continue");

    puts("interrupted erase of an old area can't make it look newer:");
    reboot();
    int active = (valid(1) && gen(1) > gen(0)) ? 1 : 0, stale = 1 - active;
    flashfs_write("s.txt", "stale-test", 10);
    lost = 0;
    for (int trial = 0; trial < 300; trial++) {
        memcpy(snap, sim_mem, STORAGE_SIZE);
        /* an old, valid superblock with a lower generation, then bits flipped to 1 */
        uint8_t *sb = sim_mem + stale * FFS_AREA_SIZE;
        uint32_t m = FFS_MAGIC, g = gen(active) - 1, gi = ~g;
        memset(sb, 0xFF, 0x4000); memcpy(sb, &m, 4); sb[4] = FFS_VERSION; memcpy(sb + 8, &g, 4); memcpy(sb + 12, &gi, 4);
        for (int i = 4; i < 16; i++) sb[i] |= (uint8_t)(rand() & rand());   /* partial erase */
        reboot();
        if (!IS("s.txt", "stale-test")) lost++;
        memcpy(sim_mem, snap, STORAGE_SIZE);
    }
    CHECK(lost == 0, "300 random partial erases: the active area always wins");

    puts("streamed writes (large files):");
    flashfs_format();
    static char huge[100 * 1024];
    for (unsigned i = 0; i < sizeof huge; i++) huge[i] = (char)(i * 31 + (i >> 9));
    ok = flashfs_stream_begin("huge.bin", sizeof huge) == 0;
    for (unsigned o = 0; ok && o < sizeof huge; o += 256) ok = flashfs_stream_write(huge + o, 256) == 256;
    CHECK(ok && !flashfs_exists("huge.bin"), "100 KB streamed in 256-byte pieces; not a file until committed");
    CHECK(flashfs_stream_end() == (int)sizeof huge && content_is("huge.bin", huge, sizeof huge), "committed: reads back identical");
    reboot();
    CHECK(content_is("huge.bin", huge, sizeof huge), "and after a reboot");
    CHECK(flashfs_write("huge2.bin", huge, sizeof huge) < 0 && flashfs_stream_begin("huge2.bin", sizeof huge) < 0,
          "a second 100 KB file doesn't fit (112 KB area): refused up front");
    CHECK(flashfs_stream_begin("huge.bin", sizeof huge) < 0, "replacing it needs room for both copies: refused, old file kept");
    CHECK(content_is("huge.bin", huge, sizeof huge), "old file intact");
    flashfs_delete("huge.bin");
    flashfs_write("a.txt", "AAA", 3);
    flashfs_stream_begin("s.txt", 10);
    flashfs_stream_write("01234", 5);
    CHECK(flashfs_write("b.txt", "BBB", 3) == 3, "another file can be written while a stream is open");
    flashfs_stream_write("56789", 5);
    CHECK(flashfs_stream_end() == 10 && IS("s.txt", "0123456789") && IS("b.txt", "BBB"), "both intact: the stream's space was reserved");
    flashfs_stream_begin("s.txt", 4);
    CHECK(flashfs_stream_begin("t.txt", 4) < 0 && flashfs_stream_active(), "a second stream is refused; the first goes on");
    CHECK(flashfs_stream_write("12345", 5) < 0, "writing past the announced size refused");
    CHECK(flashfs_stream_end() < 0 && IS("s.txt", "0123456789"), "ending short of the size fails; old file kept");
    flashfs_stream_begin("s.txt", 3); flashfs_stream_write("NEW", 3); flashfs_stream_abort();
    CHECK(flashfs_stream_end() < 0 && IS("s.txt", "0123456789"), "abort keeps the old file");
    /* a compaction caused by another write ends the stream */
    flashfs_format();
    flashfs_write("keep.txt", "kept", 4);
    flashfs_stream_begin("st.txt", 8);
    flashfs_stream_write("ABCD", 4);
    static char blk[8192]; memset(blk, 'Q', sizeof blk);
    int writes = 0;
    for (int i = 0; i < 40 && flashfs_stream_active(); i++) if (flashfs_write("churn.bin", blk, sizeof blk) == (int)sizeof blk) writes++;
    CHECK(!flashfs_stream_active() && flashfs_stream_write("EFGH", 4) < 0 && flashfs_stream_end() < 0,
          "a compaction (from other writes filling the area) ends the stream");
    CHECK(!flashfs_exists("st.txt") && IS("keep.txt", "kept") && content_is("churn.bin", blk, sizeof blk), "no half file; other files intact");
    /* log full when the stream commits: committed through a compaction */
    flashfs_format();
    flashfs_stream_begin("late.txt", 6);
    flashfs_stream_write("LATE!!", 6);
    int nw = 0; while (nw < 500) { snprintf(buf, sizeof buf, "v%d", nw); if (flashfs_write("tiny.txt", buf, strlen(buf)) < 0) break; nw++; if (!flashfs_stream_active()) break; }
    printf("       (%d small writes before the stream was ended or the log filled)\n", nw);
    /* the stream may have been ended by the compaction those writes caused; if
       it survived, end() must still commit it */
    if (flashfs_stream_active()) CHECK(flashfs_stream_end() == 6 && IS("late.txt", "LATE!!"), "stream committed");
    else CHECK(!flashfs_exists("late.txt"), "stream ended by the compaction, nothing half-written");
    /* fill the log without compacting: a stream begun right after a compaction */
    flashfs_format();
    for (int i = 0; i < 400; i++) { snprintf(buf, sizeof buf, "%d", i); flashfs_write("n.txt", buf, strlen(buf)); }
    flashfs_stream_begin("end.txt", 3); flashfs_stream_write("END", 3);
    for (int i = 0; i < 2; i++) flashfs_write("n2.txt", "x", 1);
    int active_before = flashfs_stream_active();
    int r = flashfs_stream_end();
    CHECK(!active_before || (r == 3 && IS("end.txt", "END")), "end with a nearly full log commits (by compaction if needed)");
    flashfs_format();
    for (int i = 0; i < 402; i++) flashfs_write("fill.txt", "abcd", 4);       /* 402 of 403 log records */
    flashfs_stream_begin("full.txt", 5); flashfs_stream_write("FULL!", 5);
    flashfs_write("fill.txt", "last", 4);                                    /* the 403rd: log now full */
    int still = flashfs_stream_active();
    CHECK(still && flashfs_stream_end() == 5 && IS("full.txt", "FULL!") && IS("fill.txt", "last"),
          "log exactly full at commit: committed by a compaction that copies the streamed data");
    reboot();
    CHECK(IS("full.txt", "FULL!") && IS("fill.txt", "last"), "and after a reboot");
    puts("power cut at every step of a streamed write:");
    flashfs_format();
    flashfs_write("old.txt", "OLD", 3);
    flashfs_write("other.txt", "other", 5);
    memcpy(snap, sim_mem, STORAGE_SIZE);
    static char data2[3000]; memset(data2, 'N', sizeof data2);
    int lost2 = 0, steps2 = 0;
    for (long k = 1; k < 200; k++) {
        memcpy(sim_mem, snap, STORAGE_SIZE); reboot();
        sim_ops = 0; sim_cut_at = k;
        int cut = setjmp(sim_cut);
        if (!cut) {
            flashfs_stream_begin("old.txt", sizeof data2);
            for (unsigned o = 0; o < sizeof data2; o += 500) flashfs_stream_write(data2 + o, 500);
            flashfs_stream_end();
        }
        sim_cut_at = -1;
        reboot();
        if (!(IS("old.txt", "OLD") || content_is("old.txt", data2, sizeof data2)) || !IS("other.txt", "other")) lost2++;
        if (flashfs_write("after.txt", "ok", 2) != 2) lost2++;
        if (!cut) { steps2 = (int)k; break; }
    }
    printf("       (%d steps)\n", steps2);
    CHECK(steps2 > 5 && lost2 == 0, "after any cut: the old or the new file, never a mix; writes work");

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
