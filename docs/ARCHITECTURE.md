# NumWorks OS — Architecture

## Hardware target

| Item | Detail |
|------|--------|
| MCU | STM32F730V8T6 — Cortex-M7 @ 216 MHz, single-precision FPU |
| Flash | 64 KB internal (bootloader) + 8 MB external QSPI (AT25SF641): code and files |
| RAM | 256 KB in total: 64 KB DTCM @ 0x2000_0000, 176 KB SRAM1, 16 KB SRAM2 |
| Display | 320×240, ST7789V controller on a 16-bit 8080 bus through FMC bank 1 (0x6000_0000) |
| Input | 9×6 key matrix, NumWorks' 46-key layout |
| USB | OTG_FS (PA11/PA12) |
| Power | Li-ion battery, RT9526A linear charger |

**The NumWorks N0120 is a different machine.** Its stock firmware uses RAM
at 0x2400_0000 (STM32H7 AXI-SRAM), which the F730 doesn't have. Running
on an N0120 means porting the register definitions, clocks, flash, GPIO
and memory map to the STM32H7 using the N0120 schematic.

Not yet verified against a schematic: the keyboard row/column pins, the
LCD (reset, backlight, mirroring), the LED and the charger status pins.
The LED driver (`hal/led.c`) stays off until its pins are set in
`include/config.h`; if the picture comes out mirrored or with red and
blue swapped, change `LCD_MADCTL` there (see *Display*).

## Memory map

```
QSPI flash (XIP)   0x9000_0000  vector table, code, read-only data
                                (~95 KB; ~200 KB with MicroPython;
                                the linker allows 8 MB − 256 KB)
                   0x907C_0000  file system, 2 × 128 KB (last 256 KB)

DTCM  0x2000_0000  16 KB  stack (grows down from 0x2000_4000; an
                          overflow faults below 0x2000_0000)
      0x2000_4000  48 KB  MicroPython heap
SRAM  0x2001_0000 150 KB  framebuffer (320×240×2)
      0x2003_5800   3 KB  .data, including the QSPI flash routines
                          (they must not run from the QSPI flash)
      then         ~29 KB .bss
      then  ... 0x2004_0000  newlib heap (~10 KB)
```

Internal flash (64 KB on the F730x8) holds NumWorks' bootloader and is
left alone.

`linker/numworks_n0120.ld` is the linker script; it asserts the stack
and heap minimums at link time. The stack is filled with a pattern at
boot, so `mem` can show how deep it has been.

## Boot

`Reset_Handler` masks interrupts, sets the stack pointer, points VTOR at
our vector table, disables interrupts a bootloader left enabled, copies
`.data`, clears `.bss`, enables the FPU and unmasks interrupts. Then
`boot_main()`:

1. MemManage, BusFault and UsageFault are enabled as separate
   exceptions (otherwise they all arrive as HardFault);
2. clocks: switch to HSI, set up the PLL (8 MHz HSE, or HSI if the
   crystal doesn't start), enable over-drive, switch to 216 MHz;
3. MPU: the LCD bus at 0x6000_0000 becomes Device memory; region 7
   (no access) is reserved for the QSPI window while it is being
   written (see *Storage*);
4. I-cache on (code runs from QSPI); D-cache stays off;
5. SysTick at 1 kHz.

`main()` then starts the UART, the watchdog, the LCD, keyboard, timer,
LED, kernel, file system, USB (device side) and MicroPython, shows the
splash and opens the home screen (or the shell, if HOME is held).

If the file system isn't there (first boot, or the area was damaged),
the calculator asks before formatting: **OK** formats, **BACK** carries
on without files. If the flash itself isn't usable (unexpected JEDEC
ID, write protection, QSPI not set up by the bootloader), the reason is
shown and logged and the system runs without files.

The debug UART (USART1, 115200 8N1) logs the reset cause (power-on,
reset pin, watchdog, software), whether the crystal started, and each
boot stage with a timestamp:

```
NumWorks OS v0.2 booting...
reset cause: power on / brown-out
clock: 216 MHz from HSE
[boot    12 ms] display
...
[boot  1890 ms] apps; starting event loop
```

## Crashes

Every fault handler, and any interrupt without a handler, goes to
`fault_report()` (`hal/fault.c`). It writes the fault type, a likely
cause (from CFSR), PC, LR, the fault address and the uptime to the UART
and to the screen, then waits for a key and resets. If the stack has
overflowed, the handler first switches to a fresh stack and doesn't
read the (missing) exception frame. `assert()`, `abort()`/`exit()` and
MicroPython's last-resort error handler use the same screen through
`hal_panic()`.

**Watchdog.** The independent watchdog (IWDG, on the 32 kHz LSI) resets
the calculator if the firmware stops feeding it for about 8 seconds.
The kernel loop, `hal_delay_ms()`, the Python VM hook, flash waits and
the crash screen feed it, so a slow script or a long erase doesn't
trip it, but a hang with interrupts off does. After such a reset the
splash says so. It is paused while a debugger halts the CPU;
`WATCHDOG_ENABLED` in `include/config.h` turns it off. Once started it
can't be stopped, so sleep mode keeps feeding it too.

## Kernel

A cooperative scheduler with four tasks:

```
input    scan the keyboard (every 5 ms), queue events
display  push the changed rectangle of the framebuffer to the LCD
app      hand events to the current app, run its tick(), USB protocol
idle     WFI until the next interrupt
```

Each task sleeps one SysTick after running; when none is ready the idle
task puts the CPU to sleep. Interrupts are masked around the check, so
a wake-up can't be lost.

**Sleep.** ON/OFF, or `AUTO_SLEEP_MS` (5 minutes) without a key press,
puts the LCD to sleep and slows SysTick to one tick per 20 ms, so the
CPU wakes 50 times a second instead of 1000. Only the keyboard and the
PC transfer protocol are serviced; ON/OFF wakes up and the app is
redrawn. The LED is switched off and gets its colour back on wake-up.
The backlight stays on until its control pin is known.

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

## Display

`hal/display.c` drives the ST7789V through FMC bank 1: writes to
0x6000_0000 are commands, writes with address line A16 set are data
(D/CX). The bus timings come from the ST7789V's 8080 write cycle
(66 ns minimum; we use about 69 ns). If the bootloader already set up
the FMC pins they are left as they are, otherwise they are configured
as alternate function 12.

Start-up follows the datasheet: software reset, wait 120 ms, sleep
out, wait, 16-bit colour (`COLMOD 0x55`), `MADCTL` from
`LCD_MADCTL`, display on. The controller's ID (`RDDID`, 85 85 52 for an
ST7789V) is written to the UART log, which tells whether the bus works
at all.

Drawing goes into a RAM framebuffer. Every drawing call grows a
*dirty rectangle*, and `display_flush()` only sends that window
(`CASET`/`RASET`/`RAMWR`), so a blinking cursor costs a few hundred
pixels rather than 76 800.

## Storage

The file system lives in the last 256 KB of the QSPI flash
(0x907C_0000). Code runs from the same chip, memory-mapped, so writing
to it needs care (`fs/storage_qspi.c`):

- the routines that talk to the flash run from RAM (`.ramfunc`, copied
  with `.data`) with interrupts off, because nothing may be read from
  the QSPI window while it is in command mode;
- the MPU blocks the window meanwhile, so a stray access faults
  instead of reading garbage;
- the bootloader's QSPI settings (SPI or quad-instruction mode,
  continuous-read mode) are read back and restored afterwards;
- programs are split at 256-byte pages, erases use 64 KB blocks when
  aligned (else 4 KB sectors), and every write and erase is read back
  and checked. Waits are bounded by the AT25SF641's maximum times.

At start-up the driver checks the controller set-up, the JEDEC ID
(1F 32 17) and the block-protection bits, and refuses to write if
anything is unexpected.

## Flash file system (flashfs)

Two areas of 128 KB; one is active. Layout of an area:

```
[0x0000 – 0x00FF]   superblock: magic, version, generation (+ inverse)
[0x0100 – 0x3FFF]   record log: 403 × 40-byte records, append-only
[0x4000 – end]      file data, append-only (112 KB)
```

Flash can only be programmed from the erased state, so nothing is
rewritten in place:

- Writing a file appends its data, then a record pointing at it.
- Deleting or renaming appends a record.
- The newest record for a name wins.
- A record only counts once its last word is programmed, so a power cut
  mid-write keeps the previous version.

When the log or the data area is full, the live files are copied into
the other area, which gets the next generation number. Its superblock's
magic is programmed last, and at start-up the valid area with the
highest generation wins, so a power cut *during* compaction leaves the
old area, with all files, in use. The generation is also stored
inverted, so a half-finished erase can't make a stale area look newer.

Limits: 32 files, 8 KB per file, names up to 23 characters.

The flash is memory-mapped, so `flashfs_map()` hands out a file's
contents in place (valid until the next write). Python reads scripts
and modules this way instead of copying them to RAM.

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
- `open(name, mode)`: `"r"` returns an `io.StringIO` of the file and
  `"rb"` an `io.BytesIO`; `"w"`, `"a"` (and `"wb"`, `"ab"`) return a
  file whose `write()` collects text in RAM and saves it on `close()`,
  at the end of a `with` block, after each REPL entry, at the end of a
  script, or when leaving the Python app (at most 4 open for writing,
  8 KB each). `print(..., file=f)` isn't supported; use `f.write()`;
- `os`: `listdir()`, `remove()`, `rename()`, `stat()` (size at
  index 6);
- `import name` loads `name.py` from flash (no packages: the file
  system is flat);
- `input()` reads a line from the keypad;
- `time`: `sleep`, `sleep_ms`, `sleep_us`, `ticks_ms`, `ticks_us`,
  `ticks_add`, `ticks_diff`, and `time()` / `monotonic()` counting from
  power-on (there is no clock);
- `random`: `seed`, `random`, `uniform`, `randint`, `randrange`,
  `choice`, `getrandbits`.

`time`, `random` and `os` are our own modules in `modules/nwos/` (`random`
is adapted from MicroPython's), because the embed port doesn't ship
MicroPython's `extmod`. For the same reason
`micropython-port/shared/readline/readline.h` stands in for
MicroPython's, so `input()` calls our keypad reader.

The app running Python sets a console (`mp_console_t`: write, read a
line, show). Without one, Python uses the shell. Output reaches the
screen while a script runs (at most every 50 ms, and before each
sleep or `input()`).

The `display` module offers:
- `fill(c)`
- `str(x, y, text[, fg[, bg]])`
- `pixel(x, y, c)`
- `fill_rect(x, y, w, h, c)`
- `rgb(r, g, b)`
- `flush()`
- colour constants.

Once a script draws with `display`, the console stays off the screen
until it ends; the drawing then stays up until a key is pressed.

A VM hook polls the keyboard so BACK interrupts a running script, and
also a `time.sleep()` or `input()`. Scripts started with `run` print to
the shell; `run` re-reads imported modules, so edits take effect.

The Python app works like a `>>>` prompt. A line that opens a block
(`def`, `for`, `if`, ...) or leaves a bracket open continues with
`... `, and the next line is indented for you (BACKSPACE on the
indentation removes a level). An empty line runs the block. UP recalls
the previous line. Leaving the app makes the next `import` read the
files again.

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
| `mem` | Stack peak, C heap, Python heap and flash usage |
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
