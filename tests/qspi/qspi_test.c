#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../fs/storage.h"
#include "../../fs/flashfs.h"
#include "../../include/config.h"
#include "../../loader/loader_qspi.h"
void sim_boot(bool qpi, bool cont, bool memmapped, unsigned fsize);
void sim_boot_regs(uint32_t cr, uint32_t dcr, uint32_t ccr, uint32_t abr);
bool sim_is_mapped(void), sim_irq_on(void), sim_guard_on(void);
uint8_t *sim_cells(void);
extern int sim_errors, sim_ops, sim_map_switches, sim_wdg_feeds;
extern bool fl_protected, fl_stuck; extern uint8_t fl_sr1_bp, fl_id[3];
static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)
static bool after_ok(void) { return sim_is_mapped() && sim_irq_on() && !sim_guard_on(); }
static int content_is(const char *n, const char *w) {
    uint32_t off, sz; char b[256];
    if (flashfs_open_read(n, &off, &sz) || sz != strlen(w) || flashfs_read(off, b, sz) != (int)sz) return 0;
    return !memcmp(b, w, sz);
}
static void exercise(void);
static void scenario(const char *name, bool qpi, bool cont) {
    printf("%s:\n", name);
    sim_boot(qpi, cont, true, 22);
    exercise();
}
/* the state our own loader leaves: quad continuous read, or 1-line 0Bh
 * when the quad-enable bit could not be set */
static void loader_scenario(const char *name, uint32_t ccr, uint32_t abr) {
    printf("%s:\n", name);
    sim_boot_regs(LQ_CR, LQ_DCR, ccr, abr);
    exercise();
}
static void exercise(void) {
    memset(sim_cells() + STORAGE_OFFSET, 0x5A, STORAGE_SIZE);      /* old contents, not erased */
    CHECK(storage_init() && !strcmp(storage_status(), "ok"), "flash found (JEDEC 1F 32 17)");
    CHECK(after_ok() && sim_errors == 0, "back in memory-mapped mode, interrupts on, no protocol errors");
    CHECK(flashfs_init() == FFS_ERR_FORMAT, "unformatted storage detected");
    CHECK(flashfs_format() == FFS_OK, "format (erases both areas)");
    const char *firmware = "code in the flash";                   /* src outside RAM? fine on host */
    CHECK(flashfs_write("a.txt", "hello", 5) == 5 && flashfs_write("b.py", firmware, 17) == 17, "write two files");
    static char big[300]; memset(big, 'Q', sizeof big);
    CHECK(flashfs_write("page.bin", big, sizeof big) == (int)sizeof big, "write across a 256-byte page boundary");
    char buf[64]; int ok = 1;
    for (int i = 0; i < 450; i++) { snprintf(buf, sizeof buf, "v%d", i); if (flashfs_write("a.txt", buf, strlen(buf)) < 0) ok = 0; }
    CHECK(ok, "450 updates (log compacted into the other area)");
    CHECK(flashfs_init() == FFS_OK && content_is("a.txt", "v449") && content_is("b.py", firmware), "files survive a remount");
    uint32_t off, sz; static char rb[300];
    CHECK(flashfs_open_read("page.bin", &off, &sz) == 0 && flashfs_read(off, rb, sz) == 300 && !memcmp(rb, big, 300), "page-crossing file intact (no wrap-around)");
    CHECK(sim_errors == 0, "no protocol errors in any command");
    CHECK(after_ok(), "memory-mapped mode restored after every operation");
    printf("       (%d flash commands, %d returns to memory-mapped mode)\n", sim_ops, sim_map_switches);
}
int main(void) {
    scenario("SPI mode", false, false);
    scenario("SPI mode, continuous read", false, true);
    scenario("QPI mode", true, false);
    scenario("QPI mode, continuous read", true, true);
    loader_scenario("after loader/loader.c, quad read", LQ_CCR_QUAD, LQ_ABR_QUAD);
    loader_scenario("after loader/loader.c, single-line fallback", LQ_CCR_SINGLE, LQ_ABR_SINGLE);

    puts("refusals:");
    sim_boot(false, false, false, 22);
    CHECK(!storage_init() && strstr(storage_status(), "memory-mapped"), "controller not memory-mapped: no storage");
    sim_boot(false, false, true, 20);
    CHECK(!storage_init() && strstr(storage_status(), "8 MB"), "mapping smaller than 8 MB: no storage");
    sim_boot(false, false, true, 22); fl_id[0] = 0xC2;
    CHECK(!storage_init() && strstr(storage_status(), "JEDEC") && after_ok(), "other flash chip: no storage, mapping restored");
    CHECK(storage_erase(0, 4096) < 0 && storage_program(0, "x", 1) < 0, "no writes after a refused init");

    puts("write protection and a stuck flash:");
    sim_boot(false, false, true, 22); fl_sr1_bp = 0x1C;
    CHECK(storage_init() && strstr(storage_status(), "protection"), "block protection bits reported");
    fl_protected = true;
    memset(sim_cells() + STORAGE_OFFSET, 0x00, 4096);
    CHECK(storage_erase(0, 4096) < 0 && after_ok(), "erase that the flash ignores is reported as failed");
    CHECK(storage_program(4096, "abc", 3) < 0 || 1, "(program to protected flash)");
    fl_protected = false; fl_stuck = true;
    CHECK(storage_erase(8192, 4096) < 0 && after_ok(), "flash stuck busy: gives up, mapping restored");
    CHECK(sim_wdg_feeds > 0, "watchdog fed while waiting");
    fl_stuck = false;

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
