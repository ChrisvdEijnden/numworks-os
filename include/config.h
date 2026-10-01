/* ================================================================
 * NumWorks OS — Central Configuration
 * Target: STM32F730V8T6 · 216 MHz Cortex-M7 (single-precision FPU)
 *         Flash 64 KB internal + 8 MB external QSPI
 *         RAM 256 KB in total, including the 64 KB DTCM
 * NOTE: the NumWorks N0120 uses an STM32H7, not this chip — see
 *       docs/ARCHITECTURE.md before running this on an N0120.
 * ================================================================ */
#pragma once

#define NWOS_VERSION        "0.2"

/* ── Clock ──────────────────────────────────────────────────── */
#define HSE_HZ            8000000UL   /* external crystal (HSI 16 MHz used if it fails) */
#define SYSCLK_HZ       216000000UL
#define APB1_HZ          54000000UL
#define APB2_HZ         108000000UL
#define TICK_HZ             1000U

/* ── UART debug ─────────────────────────────────────────────── */
#define DEBUG_BAUD          115200U

/* ── Display ────────────────────────────────────────────────── */
#define LCD_WIDTH           320
#define LCD_HEIGHT          240
#define LCD_BPP             16

/* ── Keyboard matrix ────────────────────────────────────────── */
#define KEY_ROWS            9
#define KEY_COLS            6

/* ── Flash Filesystem ───────────────────────────────────────── */
#define FFS_START           0x08070000UL
#define FFS_SIZE            (64U * 1024U)
#define FFS_SECTOR_NUM      7
#define FFS_MAX_FILES       32
#define FFS_NAME_LEN        24
#define FFS_MAX_FILE_SIZE   (8U * 1024U)

/* ── Shell ──────────────────────────────────────────────────── */
#define SHELL_LINE_LEN      80
#define SHELL_HISTORY       4

/* ── MicroPython heap ───────────────────────────────────────── */
#define MP_HEAP_SIZE        (48U * 1024U)

/* ── USB ─────────────────────────────────────────────────────── */
#define USB_RX_BUFSIZE      512U
#define USB_TX_BUFSIZE      512U

/* ── Kernel ─────────────────────────────────────────────────── */
#define TASK_STACK_WORDS    256U
#define MAX_TASKS           8U

/* ── Homepage grid ──────────────────────────────────────────── */
#define HOME_COLS           3
#define HOME_ROWS           4
#define HOME_ICON_W         88
#define HOME_ICON_H         56

/* ── RGB status LED (hal/led.c) ─────────────────────────────────
 * Not known for this board yet, so the LED stays off. To enable it, set
 * LED_CONFIGURED to 1 and define the three cathode pins from the
 * schematic (port number: 0=A, 1=B, ...):
 *   #define LED_R_PORT_NUM 1
 *   #define LED_R_PIN      4      ... and the same for G and B
 * Don't guess: the original code drove PE3, which on the N0110 is
 * believed to be the charger's status output. */
#define LED_CONFIGURED      0

/* ── Tetris ─────────────────────────────────────────────────── */
#define TETRIS_BOARD_W      10
#define TETRIS_BOARD_H      20
#define TETRIS_CELL_SZ      10

/* ── Text editor ────────────────────────────────────────────── */
#define EDITOR_MAX_BYTES    FFS_MAX_FILE_SIZE   /* any file the FS can hold */

/* ── Power ──────────────────────────────────────────────────── */
/* Screen off after this long without a key press (ON/OFF wakes) */
#define AUTO_SLEEP_MS       (5U * 60U * 1000U)
