/* ================================================================
 * NumWorks OS — QUADSPI settings the loader leaves for the OS
 * File: loader/loader_qspi.h
 *
 * Memory-mapped reads of the AT25SF641 with Fast Read Quad I/O (EBh,
 * instruction on 1 line, address and data on 4), mode byte A0h so the
 * flash stays in continuous-read mode (no instruction after the first
 * read), 4 dummy clocks (datasheet table 7-2, note 9). These are the
 * settings NumWorks' own firmware uses on the N0110.
 * The OS's storage driver (fs/storage_qspi.c) reads them back, uses its
 * own commands for writing, and restores them afterwards.
 * ================================================================ */
#pragma once

/* CR: QUADSPI clock = HCLK / 2. The loader runs at 16 MHz (8 MHz to the
 * flash); the OS then runs at 192 MHz, which gives 96 MHz, under the
 * flash's 104 MHz. */
#define LQ_CR_PRESCALER   1U
#define LQ_CR             ((LQ_CR_PRESCALER << 24) | 1U /* EN */)

/* DCR: 8 MB (FSIZE + 1 = 23 address bits); chip select high for at least
 * 5 cycles = 52 ns at 96 MHz (tSHSL >= 30 ns, NumWorks uses 50 ns);
 * clock mode 3 (CLK high while deselected), as NumWorks does. */
#define LQ_DCR            ((22U << 16) | (4U << 8) | 1U)

/* CCR fields */
#define LQ_IMODE(m)       ((uint32_t)(m) << 8)
#define LQ_ADMODE(m)      ((uint32_t)(m) << 10)
#define LQ_ADSIZE_24      (2U << 12)
#define LQ_ABMODE(m)      ((uint32_t)(m) << 14)
#define LQ_ABSIZE_8       (0U << 16)
#define LQ_DCYC(n)        ((uint32_t)(n) << 18)
#define LQ_DMODE(m)       ((uint32_t)(m) << 24)
#define LQ_FMODE_MM       (3U << 26)
#define LQ_SIOO           (1U << 28)

/* Quad I/O, continuous read: needs the QE bit in status register 2 */
#define LQ_CCR_QUAD       (0xEBU | LQ_IMODE(1) | LQ_ADMODE(3) | LQ_ADSIZE_24 | \
                           LQ_ABMODE(3) | LQ_ABSIZE_8 | LQ_DCYC(4) | LQ_DMODE(3) | \
                           LQ_FMODE_MM | LQ_SIOO)
#define LQ_ABR_QUAD       0xA0U

/* Fallback if QE can't be set: Fast Read (0Bh) on one line, 8 dummy
 * clocks. About four times slower, but needs nothing from the flash. */
#define LQ_CCR_SINGLE     (0x0BU | LQ_IMODE(1) | LQ_ADMODE(1) | LQ_ADSIZE_24 | \
                           LQ_DCYC(8) | LQ_DMODE(1) | LQ_FMODE_MM)
#define LQ_ABR_SINGLE     0x00U
