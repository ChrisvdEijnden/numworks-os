#include "qspi_sim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../../include/config.h"
#define FLASH_SIZE (8u << 20)
uint8_t *sim_view;                     /* mapped view, inaccessible while unmapped */
static uint8_t cells[FLASH_SIZE];      /* the flash array */
static uint32_t reg[9];
int sim_errors = 0;                    /* protocol violations */
static const char *last_err = "";
static void err(const char *e) { sim_errors++; last_err = e; fprintf(stderr, "SIM ERROR: %s\n", e); }
/* flash state */
bool fl_qpi = false, fl_cont = false, fl_wel = false, fl_protected = false, fl_stuck = false;
uint8_t fl_sr1_bp = 0;
uint8_t fl_id[3] = { 0x1F, 0x32, 0x17 };
static int fl_busy = 0;
static bool mapped = false, irq_on = true, guard_on = false;
int sim_wdg_feeds = 0, sim_ops = 0, sim_map_switches = 0;
uint32_t boot_ccr, boot_abr, boot_cr;
static uint8_t fifo[300]; static int fifo_n = 0, fifo_pos = 0;
static uint8_t wbuf[300]; static int wbuf_n = 0, wbuf_need = 0; static uint32_t w_addr;

static void view_protect(bool access) {
    if (access) {
        mprotect(sim_view, FLASH_SIZE, PROT_READ | PROT_WRITE);
        memcpy(sim_view + STORAGE_OFFSET, cells + STORAGE_OFFSET, STORAGE_SIZE);
        mprotect(sim_view, FLASH_SIZE, PROT_READ);
    } else {
        mprotect(sim_view, FLASH_SIZE, PROT_NONE);
    }
}
void sim_irq(bool on) { irq_on = on; }
void sim_guard(bool on) { guard_on = on; }
void sim_wdg(void) { sim_wdg_feeds++; }
static void check_safe(void) { if (!mapped && (irq_on || !guard_on)) err("unmapped with interrupts on or without the MPU guard"); }

static unsigned field(uint32_t v, int sh, int bits) { return (v >> sh) & ((1u << bits) - 1); }

static void enter_mapped(void) {
    uint32_t ccr = reg[R_CCR];
    if (fl_busy) err("mapped read while the flash is busy");
    unsigned imode = field(ccr, 8, 2);
    if (fl_cont) err("mapped mode re-entered while flash still in continuous mode (instruction would be misread)");
    if ((imode == 3) != fl_qpi) err("mapped mode instruction lines don't match QPI state");
    if (ccr != boot_ccr || reg[R_ABR] != boot_abr) err("mapped configuration not restored");
    if ((reg[R_CR] & ~0x2u) != (boot_cr & ~0x2u)) err("CR not restored");
    if (field(ccr, 14, 2) && (reg[R_ABR] & 0xF0) == 0xA0) fl_cont = true;   /* after the first access */
    mapped = true; sim_map_switches++;
    view_protect(true);
}

static void exec(void) {
    uint32_t ccr = reg[R_CCR];
    unsigned instr = ccr & 0xFF, imode = field(ccr, 8, 2), admode = field(ccr, 10, 2),
             abmode = field(ccr, 14, 2), dmode = field(ccr, 24, 2), fmode = field(ccr, 26, 2);
    unsigned lines = fl_qpi ? 3 : 1;
    check_safe();
    sim_ops++;
    fifo_n = fifo_pos = 0;
    if (fl_cont) {
        /* the flash takes the first bits as an address */
        if (imode != 0) { err("instruction sent while flash is in continuous read mode"); fl_cont = false; return; }
        if (!admode || !abmode || fmode != 1) { err("bad continuous-mode read"); return; }
        if ((reg[R_ABR] & 0xF0) != 0xA0) fl_cont = false;
        for (unsigned i = 0; i <= reg[R_DLR]; i++) fifo[fifo_n++] = cells[(reg[R_AR] + i) % FLASH_SIZE];
        reg[R_SR] |= 2;
        return;
    }
    if (imode == 0) { /* a mode-bit read when not in continuous mode: flash reads garbage instruction */
        err("read without instruction while flash is in normal mode"); return; }
    if (imode != lines) { err("instruction lines don't match the flash mode"); return; }
    if ((admode && admode != lines) || (dmode && dmode != lines)) { err("address/data lines don't match the flash mode"); return; }
    if (fl_busy && instr != 0x05) { err("command sent while busy"); return; }
    switch (instr) {
    case 0x06: fl_wel = true; reg[R_SR] |= 2; break;
    case 0x05: { uint8_t s = (fl_busy ? 1 : 0) | (fl_wel ? 2 : 0) | fl_sr1_bp;
                 if (fl_busy && !fl_stuck) fl_busy--;
                 for (unsigned i = 0; i <= reg[R_DLR]; i++) fifo[fifo_n++] = s; reg[R_SR] |= 2; break; }
    case 0x35: for (unsigned i = 0; i <= reg[R_DLR]; i++) fifo[fifo_n++] = 0x02; reg[R_SR] |= 2; break;
    case 0x9F: for (unsigned i = 0; i <= reg[R_DLR] && i < 3; i++) fifo[fifo_n++] = fl_id[i]; reg[R_SR] |= 2; break;
    case 0x20: case 0xD8: {
        uint32_t sz = instr == 0x20 ? 4096 : 65536, a = reg[R_AR];
        if (!fl_wel) { err("erase without write enable"); break; }
        if (a % sz) err("erase address not aligned");
        if (!fl_protected) memset(cells + (a & ~(sz - 1)), 0xFF, sz);
        fl_wel = false; fl_busy = 50; reg[R_SR] |= 2; break; }
    case 0x02:
        if (!fl_wel) { err("program without write enable"); break; }
        w_addr = reg[R_AR]; wbuf_n = 0; wbuf_need = (int)reg[R_DLR] + 1;
        if (wbuf_need > 256) err("page program longer than a page");
        break;
    default: err("unknown instruction");
    }
}

uint32_t sim_rd(int r) {
    if (r == R_SR) {
        uint32_t sr = reg[R_SR] & ~0x24u;
        unsigned fmode = field(reg[R_CCR], 26, 2);
        if (fmode == 1 && fifo_pos < fifo_n) sr |= 4;          /* data to read */
        if (fmode == 0) sr |= 4;                               /* room to write */
        return sr;
    }
    return reg[r];
}
void sim_wr(int r, uint32_t v) {
    switch (r) {
    case R_CR:
        if (v & 2) {                                           /* ABORT */
            if (mapped) { mapped = false; view_protect(false); }
            v &= ~2u;
        }
        reg[R_CR] = v; break;
    case R_FCR: reg[R_SR] &= ~v; break;
    case R_CCR: {
        reg[R_CCR] = v;
        unsigned fmode = field(v, 26, 2), admode = field(v, 10, 2);
        if (fmode == 3) { enter_mapped(); break; }
        if (mapped) { err("CCR written while mapped (no abort)"); break; }
        if (!admode) exec();
        break; }
    case R_AR:
        reg[R_AR] = v;
        if (mapped) { err("AR written while mapped"); break; }
        if (field(reg[R_CCR], 10, 2)) exec();
        break;
    default: reg[r] = v;
    }
}
uint8_t sim_dr_rd8(void) { check_safe(); return fifo_pos < fifo_n ? fifo[fifo_pos++] : 0xEE; }
void sim_dr_wr8(uint8_t v) {
    check_safe();
    if (wbuf_n >= wbuf_need) { err("too many data bytes"); return; }
    wbuf[wbuf_n++] = v;
    if (wbuf_n == wbuf_need) {
        /* real flash wraps inside the page */
        for (int i = 0; i < wbuf_n; i++) {
            uint32_t a = (w_addr & ~255u) | ((w_addr + (uint32_t)i) & 255u);
            if (!fl_protected) cells[a] &= wbuf[i];
        }
        fl_wel = false; fl_busy = 3; reg[R_SR] |= 2;
    }
}
/* Put the controller and flash in a state a bootloader might leave */
void sim_boot(bool qpi, bool cont, bool memmapped, unsigned fsize) {
    static bool init = false;
    if (!init) { sim_view = mmap(NULL, FLASH_SIZE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); memset(cells, 0xFF, sizeof cells); init = true; }
    fl_qpi = qpi; fl_cont = false; fl_wel = false; fl_busy = 0; fl_protected = false; fl_stuck = false; fl_sr1_bp = 0;
    fl_id[0] = 0x1F; fl_id[1] = 0x32; fl_id[2] = 0x17;
    unsigned l = qpi ? 3 : 1;
    boot_cr  = 1u | (3u << 8) | (1u << 24);                   /* EN, FTHRES 3, prescaler 1 */
    boot_ccr = 0xEB | (l << 8) | (3u << 10) | (2u << 12) | (3u << 14) | (0u << 16) | (4u << 18) | (3u << 24) |
               ((memmapped ? 3u : 0u) << 26) | (cont ? (1u << 28) : 0);
    boot_abr = cont ? 0xA0 : 0x00;
    reg[R_CR] = boot_cr; reg[R_DCR] = fsize << 16; reg[R_ABR] = boot_abr; reg[R_SR] = 0;
    mapped = false; irq_on = true; guard_on = false;
    reg[R_CCR] = boot_ccr;
    if (memmapped) { enter_mapped(); }
    sim_errors = 0; sim_ops = 0; sim_map_switches = 0;
}
/* Start from exactly these register values, already memory-mapped:
 * the state loader/loader.c hands to the OS */
void sim_boot_regs(uint32_t cr, uint32_t dcr, uint32_t ccr, uint32_t abr) {
    sim_boot(false, false, false, field(dcr, 16, 5));
    boot_cr = cr; boot_ccr = ccr; boot_abr = abr;
    reg[R_CR] = cr; reg[R_DCR] = dcr; reg[R_ABR] = abr; reg[R_CCR] = ccr;
    enter_mapped();
    sim_errors = 0; sim_ops = 0; sim_map_switches = 0;
}
bool sim_is_mapped(void) { return mapped; }
bool sim_irq_on(void) { return irq_on; }
bool sim_guard_on(void) { return guard_on; }
uint8_t *sim_cells(void) { return cells; }
