/* ================================================================
 * NumWorks OS — File storage on the external QSPI flash (AT25SF641)
 * File: fs/storage_qspi.c
 *
 * The firmware itself runs from this flash, mapped at 0x9000_0000 by
 * the QUADSPI controller ("memory-mapped mode", set up by the
 * bootloader). Erasing or programming needs the controller in
 * "indirect mode", and while it is, nothing can be read from the flash:
 * no code, no constants, no vector table. So every operation:
 *
 *   - runs from RAM (RAMFUNC: the .ramfunc section is copied to RAM at
 *     boot with .data), calls nothing outside RAM, and keeps its data
 *     in RAM (bounce buffer);
 *   - masks interrupts (their handlers and the vector table are in the
 *     flash) and blocks any access to 0x9000_0000 with an MPU region,
 *     so the Cortex-M7 can't fetch from it speculatively;
 *   - saves the bootloader's memory-mapped configuration and restores
 *     it afterwards, whatever it was.
 *
 * Two bootloader settings change how commands must be sent, and both
 * are read from the saved configuration (AT25SF641 datasheet §4.4,
 * §7.14):
 *   - QPI mode (instruction on four lines): commands are then sent on
 *     four lines too, otherwise on one;
 *   - continuous read mode (mode bits Axh): the flash then takes the
 *     next bytes as an address, not a command, so a read with other
 *     mode bits is done first to end it.
 *
 * The file system uses the last STORAGE_SIZE bytes of the 8 MB chip;
 * the linker script keeps the firmware image out of them.
 * ================================================================ */
#include "storage.h"
#include "../include/config.h"
#include <string.h>

#ifdef QSPI_SIM
#include "qspi_sim.h"        /* host tests: simulated registers and flash */
#else
#include "../include/stm32f730.h"
#define REG_RD(r)      (QUADSPI->r)
#define REG_WR(r, v)   (QUADSPI->r = (v))
#define DR_RD8()       (*(volatile uint8_t *)&QUADSPI->DR)
#define DR_WR8(v)      (*(volatile uint8_t *)&QUADSPI->DR = (v))
#define IRQ_OFF()      __asm volatile("cpsid i" ::: "memory")
#define IRQ_ON()       __asm volatile("cpsie i" ::: "memory")
#define BARRIER()      __asm volatile("dsb\n isb" ::: "memory")
#define WDG_FEED()     (IWDG->KR = 0xAAAAU)
#define RAMFUNC        __attribute__((section(".ramfunc"), noinline, long_call))
#define INLINE         static inline __attribute__((always_inline))
#define MAP_BASE       0x90000000UL
/* MPU region 7: 0x9000_0000, 256 MB, no access, execute-never */
#define GUARD_ON()     do { MPU_RNR = 7; MPU_RBAR = 0x90000000UL; \
                            MPU_RASR = MPU_RASR_XN | MPU_RASR_SIZE(28) | MPU_RASR_ENABLE; \
                            BARRIER(); } while (0)
#define GUARD_OFF()    do { MPU_RNR = 7; MPU_RASR = 0; BARRIER(); } while (0)
#endif

/* ── QUADSPI register fields (RM0431 §14.5) ─────────────────────── */
#define CR_EN          (1U << 0)
#define CR_ABORT       (1U << 1)
#define CR_FTHRES_MASK (0x1FU << 8)
#define SR_TCF         (1U << 1)
#define SR_FTF         (1U << 2)
#define SR_BUSY        (1U << 5)
#define FCR_ALL        0x1BU                   /* CTEF CTCF CSMF CTOF */
#define CCR_IMODE(m)   ((uint32_t)(m) << 8)
#define CCR_ADMODE(m)  ((uint32_t)(m) << 10)
#define CCR_ADSIZE_24  (2U << 12)
#define CCR_DMODE(m)   ((uint32_t)(m) << 24)
#define CCR_FMODE_IW   (0U << 26)              /* indirect write */
#define CCR_FMODE_IR   (1U << 26)              /* indirect read */
#define CCR_FMODE_MASK (3U << 26)
#define CCR_FMODE_MM   (3U << 26)              /* memory-mapped */
#define CCR_INSTR_MASK 0xFFU
#define CCR_IMODE_MASK (3U << 8)
#define CCR_SIOO       (1U << 28)
#define DCR_FSIZE(d)   (((d) >> 16) & 0x1FU)   /* flash size = 2^(FSIZE+1) */

/* ── AT25SF641 commands (datasheet tables 7-2, 7-5) ─────────────── */
#define CMD_WREN       0x06
#define CMD_RDSR1      0x05
#define CMD_RDSR2      0x35
#define CMD_RDID       0x9F
#define CMD_PP         0x02     /* page program, up to 256 bytes */
#define CMD_BE4K       0x20
#define CMD_BE64K      0xD8
#define SR1_BUSY       0x01
#define SR1_BP_MASK    0x1C     /* block protect bits BP2..BP0 */
#define PAGE_SIZE      256U
#define JEDEC_ADESTO   0x1F

/* Status polls before giving up. A 64 KB erase takes up to 2 s (tBE2);
 * a poll is a few hundred ns, so this is well over that. */
#define BUSY_POLLS     20000000UL
#define FLAG_POLLS     1000000UL

enum { OP_INFO, OP_ERASE, OP_PROGRAM };

/* Saved memory-mapped configuration and how to talk to the flash. All
 * in RAM (.bss/.data), since the RAM functions read them. */
static uint32_t s_cr, s_ccr, s_abr;
static uint32_t s_lines;          /* 1: commands on one line, 3: QPI */
static bool     s_cont;           /* continuous read mode while mapped */
static uint8_t  s_bounce[PAGE_SIZE];
static uint8_t  s_info[5];        /* JEDEC ID (3), SR1, SR2 */
static bool     s_ok;
static const char *s_status = "not checked";

/* ── RAM-only helpers (inlined into the RAM functions) ─────────── */
INLINE int wait_flag(uint32_t flag) {
    for (uint32_t n = 0; n < FLAG_POLLS; n++) {
        if (REG_RD(SR) & flag) return 0;
    }
    return -1;
}

INLINE int wait_done(void) {
    int r = wait_flag(SR_TCF);
    REG_WR(FCR, FCR_ALL);
    return r;
}

INLINE uint32_t cmd_base(uint8_t instr) {
    return (uint32_t)instr | CCR_IMODE(s_lines);
}

/* Instruction only (write enable) */
INLINE int cmd(uint8_t instr) {
    REG_WR(CCR, cmd_base(instr) | CCR_FMODE_IW);   /* starts the command */
    return wait_done();
}

/* Instruction, then n bytes read back (status registers, ID) */
INLINE int cmd_read(uint8_t instr, uint8_t *out, uint32_t n) {
    REG_WR(DLR, n - 1U);
    REG_WR(CCR, cmd_base(instr) | CCR_DMODE(s_lines) | CCR_FMODE_IR);
    for (uint32_t i = 0; i < n; i++) {
        if (wait_flag(SR_FTF | SR_TCF) != 0) return -1;
        out[i] = DR_RD8();
    }
    return wait_done();
}

/* Instruction + 24-bit address (erase) */
INLINE int cmd_addr(uint8_t instr, uint32_t addr) {
    REG_WR(CCR, cmd_base(instr) | CCR_ADMODE(s_lines) | CCR_ADSIZE_24 | CCR_FMODE_IW);
    REG_WR(AR, addr);                                /* starts the command */
    return wait_done();
}

INLINE int wait_ready(void) {
    for (uint32_t n = 0; n < BUSY_POLLS; n++) {
        uint8_t sr;
        if (cmd_read(CMD_RDSR1, &sr, 1) != 0) return -1;
        if (!(sr & SR1_BUSY)) return 0;
        if ((n & 0xFFFU) == 0) WDG_FEED();
    }
    return -1;
}

/* End continuous read mode: one read, without instruction, whose mode
 * bits aren't Axh (datasheet §7.14). Same lines and dummy cycles as
 * the mapped read. */
INLINE int end_continuous_read(void) {
    uint32_t ccr = (s_ccr & ~(CCR_INSTR_MASK | CCR_IMODE_MASK | CCR_FMODE_MASK | CCR_SIOO))
                 | CCR_FMODE_IR;
    REG_WR(DLR, 0);
    REG_WR(ABR, 0x00);
    REG_WR(CCR, ccr);
    REG_WR(AR, 0);                                   /* starts the read */
    if (wait_flag(SR_FTF | SR_TCF) != 0) return -1;
    (void)DR_RD8();
    return wait_done();
}

INLINE void leave_mapped(void) {
    REG_WR(CR, REG_RD(CR) | CR_ABORT);
    for (uint32_t n = 0; n < FLAG_POLLS && (REG_RD(CR) & CR_ABORT); n++) {}
    for (uint32_t n = 0; n < FLAG_POLLS && (REG_RD(SR) & SR_BUSY); n++) {}
    REG_WR(FCR, FCR_ALL);
    REG_WR(CR, s_cr & ~CR_FTHRES_MASK);              /* FIFO threshold: 1 byte */
}

INLINE void enter_mapped(void) {
    for (uint32_t n = 0; n < FLAG_POLLS && (REG_RD(SR) & SR_BUSY); n++) {}
    REG_WR(FCR, FCR_ALL);
    REG_WR(CR, s_cr);
    REG_WR(ABR, s_abr);
    REG_WR(CCR, s_ccr);                              /* back to memory-mapped */
}

/* One operation with the flash unmapped. Nothing here may touch the
 * external flash: no calls out of RAM, no constants from .rodata. */
RAMFUNC static int qspi_op(int op, uint8_t instr, uint32_t addr, uint32_t len) {
    int r = 0;
    IRQ_OFF();
    GUARD_ON();
    leave_mapped();
    if (s_cont) r = end_continuous_read();

    if (r == 0 && op == OP_INFO) {
        r = cmd_read(CMD_RDID, s_info, 3);
        if (r == 0) r = cmd_read(CMD_RDSR1, &s_info[3], 1);
        if (r == 0) r = cmd_read(CMD_RDSR2, &s_info[4], 1);
    } else if (r == 0 && op == OP_ERASE) {
        r = cmd(CMD_WREN);
        if (r == 0) r = cmd_addr(instr, addr);
        if (r == 0) r = wait_ready();
    } else if (r == 0 && op == OP_PROGRAM) {
        r = cmd(CMD_WREN);
        if (r == 0) {
            REG_WR(DLR, len - 1U);
            REG_WR(CCR, cmd_base(CMD_PP) | CCR_ADMODE(s_lines) | CCR_ADSIZE_24 |
                        CCR_DMODE(s_lines) | CCR_FMODE_IW);
            REG_WR(AR, addr);
            for (uint32_t i = 0; i < len && r == 0; i++) {
                r = wait_flag(SR_FTF);
                if (r == 0) DR_WR8(s_bounce[i]);
            }
            if (r == 0) r = wait_done();
        }
        if (r == 0) r = wait_ready();
    }

    enter_mapped();
    GUARD_OFF();
    IRQ_ON();
    return r;
}

/* ── Public interface (runs from flash, in memory-mapped mode) ─── */
static const char *check_controller(void) {
#ifndef QSPI_SIM
    if (!(RCC->AHB3ENR & RCC_AHB3ENR_QSPIEN)) return "QUADSPI not running";
#endif
    uint32_t cr = REG_RD(CR), ccr = REG_RD(CCR);
    if (!(cr & CR_EN) || (ccr & CCR_FMODE_MASK) != CCR_FMODE_MM)
        return "QUADSPI not in memory-mapped mode";
    uint32_t imode = (ccr >> 8) & 3U;
    if (imode != 1 && imode != 3) return "unsupported QUADSPI instruction mode";
    uint32_t fsize = DCR_FSIZE(REG_RD(DCR));
    if (fsize < 22) return "QUADSPI mapping smaller than 8 MB";

    s_cr    = cr & ~CR_ABORT;
    s_ccr   = ccr;
    s_abr   = REG_RD(ABR);
    s_lines = imode;
    uint32_t abmode = (ccr >> 14) & 3U, absize = (ccr >> 16) & 3U;
    s_cont  = abmode != 0 && absize == 0 && (s_abr & 0xF0U) == 0xA0U;
    return NULL;
}

bool storage_init(void) {
    s_ok = false;
    const char *err = check_controller();
    if (err) { s_status = err; return false; }
    if (qspi_op(OP_INFO, 0, 0, 0) != 0) { s_status = "flash not responding"; return false; }
    if (s_info[0] != JEDEC_ADESTO || s_info[2] != 0x17) {
        s_status = "unexpected flash chip (JEDEC ID)";
        return false;
    }
    s_status = (s_info[3] & SR1_BP_MASK) ? "ok, but block protection bits are set"
                                         : "ok";
    s_ok = true;
    return true;
}

const char *storage_status(void) { return s_status; }

const uint8_t *storage_base(void) {
    return (const uint8_t *)(MAP_BASE + STORAGE_OFFSET);
}

uint32_t storage_size(void) { return STORAGE_SIZE; }

/* Read back through the memory map: the only way to notice a write
 * that the flash ignored (e.g. a protected block) */
static bool erased(uint32_t offset, uint32_t len) {
    const uint8_t *p = storage_base() + offset;
    for (uint32_t i = 0; i < len; i++) if (p[i] != 0xFF) return false;
    return true;
}

int storage_erase(uint32_t offset, uint32_t len) {
    if (!s_ok || offset % STORAGE_ERASE_SIZE || len % STORAGE_ERASE_SIZE ||
        offset > STORAGE_SIZE || len > STORAGE_SIZE - offset) return -1;
    while (len) {
        uint32_t addr = STORAGE_OFFSET + offset;
        /* 64 KB blocks where possible: 0.3 s instead of 16 x 60 ms */
        bool big = (addr % 0x10000U) == 0 && len >= 0x10000U;
        uint32_t n = big ? 0x10000U : STORAGE_ERASE_SIZE;
        if (!erased(offset, n)) {
            if (qspi_op(OP_ERASE, big ? CMD_BE64K : CMD_BE4K, addr, 0) != 0) return -1;
            if (!erased(offset, n)) return -1;
        }
        offset += n; len -= n;
    }
    return 0;
}

int storage_program(uint32_t offset, const void *src, uint32_t len) {
    if (!s_ok || offset > STORAGE_SIZE || len > STORAGE_SIZE - offset) return -1;
    const uint8_t *s = (const uint8_t *)src;
    while (len) {
        uint32_t addr = STORAGE_OFFSET + offset;
        uint32_t n = PAGE_SIZE - (addr % PAGE_SIZE);   /* never cross a page */
        if (n > len) n = len;
        memcpy(s_bounce, s, n);                         /* src may be in the flash */
        if (qspi_op(OP_PROGRAM, CMD_PP, addr, n) != 0) return -1;
        if (memcmp(storage_base() + offset, s_bounce, n) != 0) return -1;
        offset += n; s += n; len -= n;
    }
    return 0;
}
