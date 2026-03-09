/* ================================================================
 * NumWorks OS — Central Configuration
 * Tune these to trade RAM vs. features.
 * ================================================================ */
#pragma once

/* ── Clock ──────────────────────────────────────────────────── */
#define SYSCLK_HZ       216000000UL   /* 216 MHz PLL */
#define APB1_HZ          54000000UL
#define APB2_HZ         108000000UL

/* ── Tick ───────────────────────────────────────────────────── */
#define TICK_HZ             1000U     /* 1 ms SysTick */

/* ── UART debug (PA9/PA10 on USART1) ───────────────────────── */
#define DEBUG_UART          USART1
#define DEBUG_BAUD          115200U

/* ── Display (NumWorks ILI9341 320×240) ─────────────────────── */
#define LCD_WIDTH           320
#define LCD_HEIGHT          240
#define LCD_BPP             16        /* RGB565 */

/* ── Keyboard matrix ────────────────────────────────────────── */
#define KEY_ROWS            9
#define KEY_COLS            6

/* ── Flash Filesystem ───────────────────────────────────────── */
#define FFS_START           0x08070000UL
#define FFS_SIZE            (64U * 1024U)
#define FFS_SECTOR_NUM      7         /* STM32F730 sector 7 */
#define FFS_MAX_FILES       32
#define FFS_NAME_LEN        24
#define FFS_MAX_FILE_SIZE   (8U * 1024U)

/* ── Shell ──────────────────────────────────────────────────── */
#define SHELL_LINE_LEN      80
#define SHELL_HISTORY       4

/* ── MicroPython heap (carved from main RAM) ────────────────── */
#define MP_HEAP_SIZE        (48U * 1024U)

/* ── USB CDC ─────────────────────────────────────────────────── */
#define USB_RX_BUFSIZE      512U
#define USB_TX_BUFSIZE      512U

/* ── Kernel task stack size ─────────────────────────────────── */
#define TASK_STACK_WORDS    256U      /* 1 KB per task */
#define MAX_TASKS           4U
