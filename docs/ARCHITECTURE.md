# NumWorks OS — Architecture

## Hardware target

| Item | Detail |
|------|--------|
| MCU | STM32F730V8T6 — Cortex-M7, run at 192 MHz, single-precision FPU |
| Flash | 64 KB internal (our loader) + 8 MB external QSPI (AT25SF641): code and files |
| RAM | 256 KB in total: 64 KB DTCM @ 0x2000_0000, 176 KB SRAM1, 16 KB SRAM2 |
| Display | 320×240, ST7789V controller on a 16-bit 8080 bus through FMC bank 1 (0x6000_0000) |
| Input | 9×6 key matrix, NumWorks' 46-key layout |
| USB | OTG_FS (PA11/PA12), VBUS sensed on PA9; ESD protection USBLC6-2 |
| Power | Li-ion battery, RT9526A linear charger, battery voltage on an ADC |

**The NumWorks N0120 is a different machine.** Its stock firmware uses RAM
at 0x2400_0000 (STM32H7 AXI-SRAM), which the F730 doesn't have. Running
on an N0120 means porting the register definitions, clocks, flash, GPIO
and memory map to the STM32H7 using the N0120 schematic.

### Board pins (N0110)

The pins come from the board configuration NumWorks published with
Epsilon 15.5 (`ion/src/device/n0110/drivers/config/` in
github.com/numworks/epsilon; only facts are taken from it, no code).
They are collected in `include/config.h`.

| Function | Pins |
|----------|------|
| Keyboard rows A–I (open drain) | PA1, PA0, PA2–PA8 |
| Keyboard columns 1–6 (pull-up) | PC0–PC5 |
| LCD bus (FMC, AF12) | PD0/1/4/5/7–11/14/15, PE7–PE15 |
| LCD power / reset / EXTC / TE | PC8 / PE1 / PD6 / PB11 |
| Backlight | PE0 |
| RGB LED (TIM3 CH1–3, AF2) | PB4 red, PB5 green, PB0 blue |
| Battery voltage ÷ 2 (ADC1 ch 9) | PB1 |
| Charger CHG (low = charging) | PE3 |
| USB D− / D+ / VBUS | PA11 / PA12 / PA9 |
| QSPI flash (set up by the loader) | PB2, PB6, PC9, PD12, PD13, PE2 |
| Debug console (USART6, AF8) | PC6 TX, PC7 RX |

Earlier versions of this OS guessed some of these, and the guesses were
wrong in ways that matter: keyboard rows on PB0–PB2 (the blue LED, the
battery sense input and the QSPI clock), the console on PA9 (USB VBUS)
and the LED on PE3 (the charger's output). Nothing has run on real
hardware yet; if the picture comes out mirrored, or with red and blue
swapped, change `LCD_MADCTL` in `include/config.h`.

## Memory map

```
QSPI flash (XIP)   0x9000_0000  vector table, code, read-only data
                                (~100 KB; ~210 KB with MicroPython;
                                the linker allows 8 MB − 256 KB)
                   0x907C_0000  file system, 2 × 128 KB (last 256 KB)

DTCM  0x2000_0000  16 KB  stack (grows down from 0x2000_4000; an
                          overflow faults below 0x2000_0000)
      0x2000_4000  48 KB  MicroPython heap
SRAM  0x2001_0000 150 KB  framebuffer (320×240×2)
      0x2003_5800   3 KB  .data, including the QSPI flash routines
                          (they must not run from the QSPI flash)
      then         ~26 KB .bss
      then  ... 0x2004_0000  newlib heap (~13 KB)
```

Internal flash (64 KB on the F730x8) holds only our loader, at
0x0800_0000 in the first 16 KB sector (`loader/`, see *Boot*).

`linker/numworks_n0120.ld` is the linker script; it asserts the stack
and heap minimums at link time. The stack is filled with a pattern at
boot, so `mem` can show how deep it has been.

## Boot

The loader in the internal flash (`loader/loader.c`) runs first. It
checks key 6 (held: ST's ROM bootloader, for recovery), brings the QSPI
flash to a known state (out of continuous-read mode and deep power-down,
not busy), maps it at 0x9000_0000 with quad reads, checks the vector
table there and jumps to it with VTOR set. It falls back to ST's
bootloader, red LED on, if the flash doesn't answer or there is no valid
image. Details and the reasons behind its settings are in
`docs/BUILD.md` (*Boot chain*) and the comments in the source.

The OS's `Reset_Handler` masks interrupts, sets the stack pointer, points VTOR at
our vector table, disables interrupts a bootloader left enabled, copies
`.data`, clears `.bss`, enables the FPU and unmasks interrupts. Then
`boot_main()`:

1. MemManage, BusFault and UsageFault are enabled as separate
   exceptions (otherwise they all arrive as HardFault);
2. clocks (`hal/clocks.c`): switch to HSI, set up the PLL (8 MHz HSE,
   or HSI if the crystal doesn't start), enable over-drive, switch to
   192 MHz. That is what NumWorks' firmware uses, and not the F730's
   216 MHz maximum, because the QSPI flash we run from is clocked at
   HCLK/2: 216 MHz would put it at 108 MHz, over the AT25SF641's
   104 MHz. 192 MHz also gives USB an exact 48 MHz (PLLQ = 8). APB1
   runs at 48 MHz, APB2 at 96 MHz;
   the cycle counter (DWT) is started for `hal_delay_us()`;
3. MPU: the LCD bus at 0x6000_0000 becomes Device memory; region 7
   (no access) is reserved for the QSPI window while it is being
   written (see *Storage*);
4. I-cache on (code runs from QSPI); D-cache stays off;
5. SysTick at 1 kHz.

`main()` then starts the UART, the watchdog, the LCD and its backlight,
keyboard, LED, battery monitor, kernel, file system, USB and
MicroPython, shows the splash and opens the home screen (or the shell,
if HOME is held).

If the file system isn't there (first boot, or the area was damaged),
the calculator asks before formatting: **OK** formats, **BACK** carries
on without files. If the flash itself isn't usable (unexpected JEDEC
ID, write protection, QSPI not set up by the loader), the reason is
shown and logged and the system runs without files.

The debug UART (USART6 on PC6/PC7, 115200 8N1) logs the reset cause (power-on,
reset pin, watchdog, software), whether the crystal started, and each
boot stage with a timestamp:

```
NumWorks OS v0.2 booting...
reset cause: power on / brown-out
clock: 192 MHz from HSE
lcd: id 4E 41 01 (NumWorks panel)
[boot   140 ms] display
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
app      hand events to the current app, run its tick(), USB protocol,
         battery check (every 2 s)
idle     WFI until the next interrupt
```

Each task sleeps one SysTick after running; when none is ready the idle
task puts the CPU to sleep. Interrupts are masked around the check, so
a wake-up can't be lost.

**Sleep.** ON/OFF, or `AUTO_SLEEP_MS` (5 minutes) without a key press,
switches the backlight off, powers the panel down (its bus pins parked)
and slows SysTick to one tick per 20 ms, so the CPU wakes 50 times a
second instead of 1000. Without USB power the core also drops to 16 MHz
on the internal oscillator, with the PLL, the crystal and over-drive
off; with USB power the clocks stay up so a PC transfer keeps working.
The LED only shows the charge state while asleep. ON/OFF wakes up: the
clocks come back, the panel is set up again and the app is redrawn.
(Stop mode would save more, but the watchdog keeps running in Stop
mode and would reset the calculator.)

Apps implement `init()`, `redraw()` and `handle_event()`. Tetris (gravity)
and the shell (UART input) also have a `tick()`. The kernel calls it
every loop while the app is shown.

## Keyboard

The matrix follows NumWorks' own key order (Epsilon's `ion::Keyboard::Key`,
index = row × 6 + column). Rows (PA1, PA0, PA2–PA8) are open-drain
outputs and columns (PC0–PC5) inputs with pull-ups. After driving a
row the driver waits 100 µs for the columns to settle, as NumWorks'
firmware does. While no key is down, one read with all rows driven
tells whether anything was pressed, so an idle keyboard costs 100 µs
per scan instead of 900. A key must read the same on two scans 5 ms
apart. Arrows and backspace repeat after 500 ms, every 100 ms. A row
or column pin that another peripheral already owns is left alone and
logged.

`key_to_char()` maps keys to characters: ALPHA gives the letters printed
on the keys, and SHIFT gives `[ ] { } = _ < > #`. The math apps use
`expr_key_text()`, which types whole tokens (`sin(`, `^2`, `pi`, `ans`).

## Display

`hal/display.c` drives the ST7789V through FMC bank 1: writes to
0x6000_0000 are commands, writes with address line A16 set are data
(D/CX). The bus timings come from the ST7789V's 8080 cycle at 192 MHz:
writes take 13 HCLK cycles (68 ns; at least 66 ns), reads 87 (453 ns;
at least 450 ns).

Start-up: the panel's power (PC8) on, EXTC (PD6) high, a hardware
reset on RESX (PE1), 120 ms, then sleep out, 16-bit colour
(`COLMOD 0x55`), `MADCTL` from `LCD_MADCTL` (0xA0, NumWorks' landscape
setting), inversion on (the N0110's panels need it), display on. The
controller's ID (`RDDID`) goes to the UART log: NumWorks' panels answer
4E xx xx (4E 41 01 and 4E 48 01 are known), a bare ST7789V 85 85 52,
and all 00 or FF means nothing answered. NumWorks loads a gamma curve
for some panels; we don't, so colours may look slightly different.
While asleep the panel is powered down and its bus pins are parked, so
nothing feeds it through its inputs.

**Backlight** (`hal/backlight.c`, PE0): the driver chip switches on at
its brightest level when the pin goes high; each 20 µs low pulse steps
one level down, wrapping from the dimmest to the brightest, and a few
milliseconds low switch it off. It can't be read back, so the driver
counts. 16 levels, set in Settings (default 12).

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
- the QSPI settings the loader left (SPI or quad-instruction mode,
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

Limits: 32 files, 100 KB per file (the area holds 112 KB of data),
names up to 23 characters. Names starting with a dot (`.settings`) are
the system's own: the file manager and `ls` don't show them.

**Streamed writes.** A file too big to hold in RAM is written with
`flashfs_stream_begin(name, size)`, `..._write()` and `..._end()`:
begin reserves the space (compacting first if needed), the data is
programmed as it arrives, and only `end` appends the record that makes
it the file, so a power cut or an abort leaves the old file as it was.
Other files can be written meanwhile, after the reservation; if one of
them triggers a compaction, the stream is ended and its `end` fails.
Replacing a file needs room for both copies until the new one is
complete. PC uploads use this, a page at a time, so there's no RAM
buffer for the whole file.

The flash is memory-mapped, so `flashfs_map()` hands out a file's
contents in place (valid until the next write). Python reads scripts
and modules this way instead of copying them to RAM.

## Math apps

`apps/common/expr.c` is a small recursive-descent evaluator:
- operators `+ - * / ^`, implicit multiplication (`2x`);
- `x`, `pi`, `e`, `ans`;
- sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, sqrt, cbrt, ln, log, exp, abs.

The Calculator, Functions, Equations and Statistics apps all use it.
The single-equation solver runs Newton's method from several starting
points.

**Calculator.** Each calculation goes into a history (the last
`CALC_HISTORY`, 20, with the whole expression), newest just above the
input. UP/DOWN select one, LEFT/RIGHT choose its calculation or result,
OK copies that into the input. An error keeps the input for fixing.

**Graph analysis** (`apps/common/analysis.c`). OK starts a trace cursor
on a function; TOOLBOX finds the next zero, minimum, maximum or
intersection to the right of the cursor, inside the visible window.
The window is sampled at 1200 points: a zero is a sign change refined by
bisection (a sign change across a pole, like 1/x at 0, is rejected
because |f| grows instead of shrinking); an extremum is a sample above
or below both neighbours, refined by golden-section search, which pins
it down to about 1e-8; an intersection is a zero of f − g.

**Statistics** (`apps/common/stats.c`). Lists X and Y of up to 100
values, typed in a table (cells accept expressions) or uploaded as
`stats.csv` (one `x,y` per line, which is also how they're saved).
One-variable summary of X: n, sum, mean, median, quartiles (medians of
the lower and upper halves, the median left out for odd n, as on the
TI-84), min, max, range, standard deviation (population and sample).
With X/Y pairs: the least-squares line y = ax + b and r. Plots: a
scatter plot with the line, or a box plot and a histogram (Sturges'
rule for the number of bins).

## Games

A Games menu holds Tetris, Snake and 2048; BACK in a game returns to
it. Snake's speed goes up with every apple; 2048 merges each tile at
most once per move. Best scores are kept in `.settings`.

## Settings and language

`apps/settings/prefs.c` keeps the LED colour, the brightness, the
language and the best scores in `.settings`, a small text file
(`key=value` lines, unknown keys ignored). It is loaded right after the
file system at boot, written when leaving Settings (only if it
changed), and when a game sets a record.

Every text on screen goes through `TR("dutch", "english")`
(`ui/lang.h`), so switching the language in Settings changes the whole
interface at once, including the help, the crash screen and error
messages.

## MicroPython

Built from MicroPython's embed port (`make mp`; see `docs/BUILD.md`).
Configuration is in `micropython-port/mpconfigport.h`:
- core features, single-precision floats, `math` and `io`;
- `open(name, mode)`: `"r"` returns an `io.StringIO` of the file and
  `"rb"` an `io.BytesIO`; `"w"`, `"a"` (and `"wb"`, `"ab"`) return a
  file whose `write()` collects text in RAM and saves it on `close()`,
  at the end of a `with` block, after each REPL entry, at the end of a
  script, or when leaving the Python app (at most 4 open for writing,
  16 KB each: they are held in the Python heap until saved; bigger
  files can come from the PC). `print(..., file=f)` isn't supported;
  use `f.write()`;
- `os`: `listdir()`, `remove()`, `rename()`, `stat()` (size at
  index 6);
- `import name` loads `name.py` from flash (no packages: the file
  system is flat);
- `input()` reads a line from the keypad;
- `time`: `sleep`, `sleep_ms`, `sleep_us`, `ticks_ms`, `ticks_us`,
  `ticks_add`, `ticks_diff`, and `time()` / `monotonic()` counting from
  power-on (there is no clock);
- `random`: `seed`, `random`, `uniform`, `randint`, `randrange`,
  `choice`, `getrandbits`;
- `kandinsky` and `ion`, the drawing and keyboard modules of NumWorks'
  own Python, so scripts written for the stock firmware run:
  `fill_rect`, `set_pixel`, `get_pixel`, `draw_string` (black on white
  by default), `color`; colours are `(r, g, b)`, names like `"red"` or
  `"#rrggbb"`. `ion.keydown(ion.KEY_OK)` is true while the key is held;
  the `KEY_*` numbers are NumWorks'. The canvas is the whole 320×240
  screen (NumWorks' is 320×222, under its status bar).

`time`, `random`, `os`, `kandinsky` and `ion` are our own modules in `modules/nwos/` (`random`
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
| `bat` | Battery voltage and level, USB power, charging |
| `fm` | Open the file manager |
| `reboot` | Reset |

The shell also reads commands from the debug UART (USART6 on PC6/PC7,
115200 8N1).

## PC file transfer protocol

One command per line (`\n` or `\r\n`); replies end in `\r\n`:

```
LIST               -> "<name> <size>" per file, then "OK"
RECV <name>        -> "DATA <size>", <size> raw bytes, then "OK"
SEND <name> <size> -> "READY"; the PC sends <size> raw bytes -> "OK"
DEL <name>         -> "OK"
failures           -> "ERR <reason>"   (not_found, too_large, no_space, ...)
```

`usb/usb_cdc.c` implements it on two ring buffers, which the USB device
stack fills and drains. SEND streams the file to flash (up to 100 KB);
if the old and new versions don't both fit, the old one is deleted
first, and if the new one still doesn't fit the reply is `ERR
no_space`. The PC side is `tools/web/uploader.html` (Chrome or Edge,
Web Serial) or `tools/upload.py`.

## USB

`usb/usb_device.c` runs the OTG_FS core as a full-speed CDC-ACM device,
so the calculator appears as a serial port (`/dev/ttyACM0`, `COMx`) with
no driver to install. Endpoints: EP0 control, EP1 bulk in/out (64-byte
packets), EP2 interrupt in (declared, unused). The core stays
soft-disconnected until VBUS is present on PA9.

Interrupts do the work. SETUP packets are handled when the core reports
the SETUP stage done; bulk OUT packets go straight into the receive
ring, and EP1 OUT is only re-armed while the ring has room for a whole
packet, so a fast PC is NAKed instead of losing data. A reply that ends
exactly on a 64-byte packet is followed by a zero-length packet, or the
PC would keep waiting for more.

VID:PID is 1209:0001, pid.codes' open-source vendor ID with its test
product ID: fine for development, but a PID of our own should be
requested from pid.codes before builds are distributed.

## Battery, charger, LED

`hal/battery.c` measures the battery through a 1:2 divider on PB1
(ADC1 channel 9, 2.8 V reference, 8 samples averaged) and reads the
RT9526A's CHG output on PE3 (low while charging) and VBUS on PA9. The
level uses NumWorks' thresholds, 3.62/3.7/3.8 V with 20 mV hysteresis.
The kernel checks every 2 seconds: the home screen shows a battery
symbol (with a bolt while charging) and warns when it's nearly empty.

The RGB LED is driven by TIM3 in PWM mode on PB4/PB5/PB0 (high = lit,
4.8 kHz, at most a quarter duty). With USB power it shows the charge
state (orange: charging, green: full) instead of the colour chosen in
Settings, also while asleep.

## Simulator

`sim/` runs the OS on a PC (`make run-sim`; using it is described in
`docs/BUILD.md`). The OS's own `main()` runs unchanged in a thread with
a 1 MB stack of its own, which stands in for the linker script's stack
region (`_sstack`..`_estack`, used by MicroPython and `mem`); the main
thread keeps the window (SDL2). The cut is as low as possible:

- **real**: all of the OS above the HAL, and three drivers: the display
  (`hal/display.c`), the keyboard (`hal/keyboard.c`) and the crash
  screen (`hal/fault.c`). The display driver and crash screen are
  copies made by `tests/gen_host.py`, with register accesses turned
  into calls; the kernel's copy turns WFI into a wait for the next tick;
- **simulated in detail**: the ST7789V (power and reset pins, sleep,
  display on, inversion, the column/row window and frame memory), so
  the window shows what the driver actually sent; the key matrix the
  keyboard driver scans; the QSPI flash's rules (erase to FF, program
  clears bits), kept in a file; and the USB core, replaced by a
  pseudo-terminal behind the real transfer protocol (`usb/usb_cdc.c`);
- **simple stand-ins**: SysTick (its ticks run in the OS thread,
  whenever the OS asks the time, waits or sleeps, so no other thread
  touches the scheduler), the UART (stdout and stdin), clocks, LED,
  backlight and battery.

A restart (`hal_reset()`) re-executes the simulator; the storage file
stays.
