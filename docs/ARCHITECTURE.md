# NumWorks OS — Architecture

## Hardware target

| Item | Detail |
|------|--------|
| MCU | STM32F730V8T6 — Cortex-M7 @ 216 MHz, single-precision FPU |
| Flash | 64 KB internal (bootloader) + 8 MB external QSPI (AT25SF641) |
| RAM | 256 KB in total: 64 KB DTCM @ 0x2000_0000, 176 KB SRAM1, 16 KB SRAM2 |
| Display | 320×240, 16-bit parallel bus through FMC bank 1 (0x6000_0000) |
| Input | 9×6 key matrix, NumWorks' 46-key layout |
| USB | OTG_FS (PA11/PA12) |
| Power | Li-ion battery, RT9526A linear charger |

**The NumWorks N0120 is a different machine.** Its stock firmware uses RAM
at 0x2400_0000 (STM32H7 AXI-SRAM), which the F730 doesn't have. Running
on an N0120 means porting the register definitions, clocks, flash, GPIO
and memory map to the STM32H7 using the N0120 schematic.

Not yet verified against a schematic: the keyboard row/column pins, the
LCD (FMC pins, reset, backlight, orientation), the LED and the charger
status pins.

## Memory map

```
QSPI flash (XIP)   0x9000_0000  vector table, code, read-only data
                                (~90 KB; ~195 KB with MicroPython)

DTCM  0x2000_0000  16 KB  stack (grows down from 0x2000_4000; an
                          overflow faults below 0x2000_0000)
      0x2000_4000  48 KB  MicroPython heap
SRAM  0x2001_0000 150 KB  framebuffer (320×240×2)
      then                .data, .bss (~32 KB)
      then  ... 0x2004_0000  newlib heap (~11 KB)

Internal flash 0x0807_0000  file system sector (only on parts with
                            512 KB of flash; on the 64 KB F730x8 the
                            file system detects this and is disabled)
```

`linker/numworks_n0120.ld` is the linker script; it asserts the stack
and heap minimums at link time.

## Boot

`Reset_Handler` masks interrupts, sets the stack pointer, points VTOR at
our vector table, disables interrupts a bootloader left enabled, copies
`.data`, clears `.bss`, enables the FPU and unmasks interrupts. Then
`boot_main()`:

1. clocks: switch to HSI, set up the PLL (8 MHz HSE, or HSI if the
   crystal doesn't start), enable over-drive, switch to 216 MHz;
2. MPU: the LCD bus at 0x6000_0000 becomes Device memory;
3. I-cache on (code runs from QSPI); D-cache stays off;
4. SysTick at 1 kHz.

`main()` then starts the UART, LCD, keyboard, timer, kernel, file
system, USB (device side) and MicroPython, shows the splash and opens
the home screen (or the shell, if HOME is held).

## Kernel

A cooperative scheduler with four tasks:

```
input    scan the keyboard (every 5 ms), queue events
display  push the framebuffer to the LCD if anything was drawn
app      hand events to the current app, run its tick(), USB protocol
idle     WFI until the next interrupt
```

Each task sleeps one SysTick after running; when none is ready the idle
task puts the CPU to sleep. Interrupts are masked around the check, so
a wake-up can't be lost.

Apps implement `init()`, `redraw()` and `handle_event()`. Tetris (gravity)
and the shell (UART input) also have a `tick()`. The kernel calls it
every loop while the app is shown.

## Keyboard

The matrix follows NumWorks' own key order (Epsilon's `ion::Keyboard::Key`,
index = row × 6 + column). Rows are open-drain outputs and columns are
inputs with pull-ups. A key must read the same on two scans 5 ms apart.
Arrows and backspace repeat after 500 ms, every 100 ms. Pins already
owned by another peripheral (alternate-function mode, e.g. the QSPI
clock on PB2) are left alone.

`key_to_char()` maps keys to characters: ALPHA gives the letters printed
on the keys, and SHIFT gives `[ ] { } = _ < > #`. The math apps use
`expr_key_text()`, which types whole tokens (`sin(`, `^2`, `pi`, `ans`).

## Flash file system (flashfs)

```
[0x000 – 0x0FF]   superblock (written once, when formatting)
[0x100 – 0xFFF]   record log: 96 × 40-byte records, append-only
[0x1000 – end]    file data, append-only (~60 KB)
```

Flash can only be programmed from the erased state, so nothing is
rewritten in place:

- Writing a file appends its data, then a record pointing at it.
- Deleting or renaming appends a record.
- The newest record for a name wins.
- A record only counts once its last word is programmed, so a power cut
  mid-write keeps the previous version.

When the log or the data area is full, the live files are compacted in a
RAM scratch buffer (the framebuffer; the current app is redrawn
afterwards), the sector is erased and the image is programmed back.
A power cut *during compaction* loses the file system; avoiding that
needs a second sector.

Limits: 32 files, 8 KB per file, names up to 23 characters.

## Math apps

`apps/common/expr.c` is a small recursive-descent evaluator:
- operators `+ - * / ^`, implicit multiplication (`2x`);
- `x`, `pi`, `e`, `ans`;
- sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, sqrt, cbrt, ln, log, exp, abs.

The Calculator, Functions (graph and table) and Equations apps all use
it. The single-equation solver runs Newton's method from several
starting points.

## MicroPython

Built from MicroPython's embed port (`make mp`; see `docs/BUILD.md`).
Configuration is in `micropython-port/mpconfigport.h`:
- core features, single-precision floats, `math` and `io`;
- `open()` is read-only and returns an `io.StringIO` of the file;
- importing files and `input()` are not supported yet.

The `display` module offers:
- `fill(c)`
- `str(x, y, text[, fg[, bg]])`
- `pixel(x, y, c)`
- `fill_rect(x, y, w, h, c)`
- `rgb(r, g, b)`
- `flush()`
- colour constants.

A VM hook polls the keyboard so BACK interrupts a running script. Scripts
started with `run` print to the shell. The Python app runs one line at a
time and shows expression values, like a `>>>` prompt.

## Shell commands

| Command | Description |
|---------|-------------|
| `help` | List commands |
| `ls` | List files with sizes |
| `cat <file>` | Print a file |
| `touch <file>` | Create an empty file |
| `rm <file>` | Delete a file |
| `mkdir <name>` | Same as `touch` (the file system is flat) |
| `echo <text>` | Print text |
| `run <file.py>` | Run a Python script |
| `mem` | RAM pool and flash usage |
| `fm` | Open the file manager |
| `reboot` | Reset |

The shell also reads commands from the debug UART (USART1, 115200 8N1).

## PC file transfer protocol

One command per line (`\n` or `\r\n`); replies end in `\r\n`:

```
LIST               -> "<name> <size>" per file, then "OK"
RECV <name>        -> "DATA <size>", <size> raw bytes, then "OK"
SEND <name> <size> -> "READY"; the PC sends <size> raw bytes -> "OK"
DEL <name>         -> "OK"
failures           -> "ERR <reason>"
```

`usb/usb_cdc.c` implements it on two ring buffers; a USB device stack
fills and drains them with `usb_cdc_rx_push()` / `usb_cdc_tx_pop()`.
That stack doesn't exist yet. The PC side is `tools/upload.py`.
