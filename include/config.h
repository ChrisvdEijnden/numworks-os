/* ================================================================
 * NumWorks OS — Central Configuration
 * Target: STM32F730V8T6 · 192 MHz Cortex-M7 (single-precision FPU)
 *         Flash 64 KB internal + 8 MB external QSPI
 *         RAM 256 KB in total, including the 64 KB DTCM
 * NOTE: the NumWorks N0120 uses an STM32H7, not this chip — see
 *       docs/ARCHITECTURE.md before running this on an N0120.
 * ================================================================ */
#pragma once

#define NWOS_VERSION        "0.2"

/* ── Clock ──────────────────────────────────────────────────── */
#define HSE_HZ            8000000UL   /* external crystal (HSI 16 MHz used if it fails) */
#define SYSCLK_HZ       192000000UL
#define APB1_HZ          48000000UL
#define APB2_HZ          96000000UL
#define TICK_HZ             1000U

/* ── UART debug ─────────────────────────────────────────────── */
#define DEBUG_BAUD          115200U

/* ── Board pins (NumWorks N0110) ─────────────────────────────
 * From the board configuration NumWorks published with Epsilon 15.5
 * (ion/src/device/n0110/drivers/config/). Port letters are GPIOA..E. */
/* Keyboard: rows A..I are open-drain outputs, columns 1..6 inputs
 * with pull-ups (hal/keyboard.c) */
#define KBD_ROW_PORT        GPIOA     /* rows A..I: PA1 PA0 PA2..PA8 */
#define KBD_COL_PORT        GPIOC     /* columns 1..6: PC0..PC5 */
/* Debug console: USART6, AF8 */
#define CONSOLE_TX_PORT     GPIOC
#define CONSOLE_TX_PIN      6
#define CONSOLE_RX_PORT     GPIOC
#define CONSOLE_RX_PIN      7
/* LCD (ST7789V on FMC bank 1) control lines */
#define LCD_POWER_PORT      GPIOC     /* high: panel powered */
#define LCD_POWER_PIN       8
#define LCD_RESET_PORT      GPIOE     /* RESX, low resets */
#define LCD_RESET_PIN       1
#define LCD_EXTC_PORT       GPIOD     /* EXTC, high: extended commands */
#define LCD_EXTC_PIN        6
#define LCD_TE_PORT         GPIOB     /* tearing effect (input, unused) */
#define LCD_TE_PIN          11
/* Backlight driver: enable / brightness pulses */
#define BACKLIGHT_PORT      GPIOE
#define BACKLIGHT_PIN       0
/* RGB LED: TIM3 channels 1-3 (AF2), high = on */
#define LED_RED_PORT        GPIOB
#define LED_RED_PIN         4
#define LED_GREEN_PORT      GPIOB
#define LED_GREEN_PIN       5
#define LED_BLUE_PORT       GPIOB
#define LED_BLUE_PIN        0
/* Battery: VBAT/2 on PB1 (ADC1 channel 9, 2.8 V reference); the
 * charger's CHG output on PE3 (open drain: low while charging) */
#define BAT_SENSE_PORT      GPIOB
#define BAT_SENSE_PIN       1
#define BAT_ADC_CHANNEL     9
#define BAT_ADC_VREF_MV     2800U
#define BAT_CHARGING_PORT   GPIOE
#define BAT_CHARGING_PIN    3
/* USB OTG_FS: D- PA11, D+ PA12 (AF10); VBUS sensed on PA9 */
#define USB_VBUS_PORT       GPIOA
#define USB_VBUS_PIN        9

/* ── Display ────────────────────────────────────────────────── */
#define LCD_WIDTH           320
#define LCD_HEIGHT          240
#define LCD_BPP             16
/* ST7789V memory access control (MADCTL, datasheet 9.1.28): MV (0x20)
 * swaps rows and columns, which the 240x320 controller needs for a
 * 320x240 picture; MY (0x80) turns it the right way up. 0xA0 is what
 * NumWorks' firmware uses. Add 0x08 if red and blue come out swapped. */
#define LCD_MADCTL          0xA0
#define LCD_INVERT          1         /* N0110 panels need INVON */

/* ── Keyboard matrix ────────────────────────────────────────── */
#define KEY_ROWS            9
#define KEY_COLS            6

/* ── Flash Filesystem ───────────────────────────────────────── */
/* The last 256 KB of the 8 MB external flash (the linker script keeps
 * the firmware out of them): two 128 KB areas, see fs/flashfs.h */
#define STORAGE_OFFSET      0x7C0000UL
#define STORAGE_SIZE        (256U * 1024U)
#define FFS_AREA_SIZE       (STORAGE_SIZE / 2U)
#define FFS_MAX_FILES       32
#define FFS_NAME_LEN        24
#define FFS_MAX_FILE_SIZE   (100U * 1024U)   /* streamed from the PC; the area holds 112 KB */

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

/* ── Tetris ─────────────────────────────────────────────────── */
#define TETRIS_BOARD_W      10
#define TETRIS_BOARD_H      20
#define TETRIS_CELL_SZ      10

/* ── Calculator ─────────────────────────────────────────────── */
#define CALC_HISTORY        20        /* calculations kept (~3 KB of RAM) */

/* ── Text editor ────────────────────────────────────────────── */
#define EDITOR_MAX_BYTES    (8U * 1024U)      /* held in RAM while editing */

/* ── Watchdog ───────────────────────────────────────────────── */
/* Reset if the firmware stops feeding it for about 8 s (independent
 * watchdog on the ~32 kHz LSI: /64, reload 4095). Set to 0 to disable,
 * e.g. while debugging without a debugger attached. */
#define WATCHDOG_ENABLED    1

/* ── Power ──────────────────────────────────────────────────── */
/* Screen off after this long without a key press (ON/OFF wakes) */
#define AUTO_SLEEP_MS       (5U * 60U * 1000U)
