# NumWorks OS — custom calculator firmware

A custom firmware for NumWorks calculators: a graphical home screen,
a calculator with history, graphing with analysis, equation solving,
statistics, MicroPython, a shell, a file manager, a text editor and
three games, in Dutch or English.

> **Status — read first.**
> - The code targets the **STM32F730** (the NumWorks N0110 family). The
>   **NumWorks N0120 uses an STM32H7**, so this firmware does **not** run
>   on an N0120 yet; that needs a port based on the N0120 schematic.
> - The board pins (keyboard, LCD, backlight, LED, battery, USB, debug
>   UART) follow the N0110 configuration NumWorks published with
>   Epsilon 15.5.
> - Nothing here has been tested on a real calculator. It starts
>   through its own small loader, which replaces NumWorks' code in the
>   internal flash. Read *Boot chain* and *Installing* in
>   `docs/BUILD.md` before flashing anything.

## Features

| App | English name | What it does | State |
|-----|-----------|--------------|-------|
| **Rekenmachine** | Calculator | Scientific calculator, `Ans`, inverse functions; a history to scroll back through and reuse | Works (native evaluator) |
| **Functies** | Functions | Up to 4 functions: graph (pan/zoom), trace cursor, zeros, minima, maxima and intersections; table | Works |
| **Vergelijkingen** | Equations | Quadratic, 2×2 linear system, f(x)=0 (Newton) | Works |
| **Statistiek** | Statistics | Lists X and Y; n, mean, median, quartiles, standard deviation; linear regression; scatter plot, box plot, histogram; kept in `stats.csv` | Works |
| **Python** | Python REPL | MicroPython: multi-line blocks, `input()`, import your own `.py` files, `math`, `time`, `random`, `os`, `display`, NumWorks' `kandinsky` and `ion`, `open()` to read and write files | Needs `make mp` |
| **Bestanden** | File manager | Open in editor, new file, delete (SHIFT twice) | Works |
| **Shell** | Shell | `ls cat touch rm echo run mem bat fm reboot`; also over UART | Works |
| **Spellen** | Games | Tetris, Snake and 2048, best scores kept | Works |
| **Docs** | Help | Built-in reference, Dutch and English | Works |
| **Instellingen** | Settings | RGB LED, screen brightness, language (Dutch/English), version, reboot; kept across restarts | Works |
| **Editor** | Text editor | Files up to 8 KB, scrolls sideways for long lines, asks a name for new files | Works |
| PC transfer | `tools/web/uploader.html`, `tools/upload.py` | List, upload, download, delete files (up to 100 KB) over USB, from Chrome/Edge or the command line | Works in simulation |

## Keys

Navigate with the arrow keys, **OK** (or **EXE**) opens or confirms,
**HOME**/**BACK** go back. Arrows and backspace repeat when held.

- In the math apps, **SHIFT** gives the inverse function
  (sin → asin, ln → e^x, √ → ∛).
- In the calculator, **UP/DOWN** walk through earlier calculations;
  **LEFT/RIGHT** pick the calculation or its result, **OK** puts it in
  the input, **BACKSPACE** removes it from the history.
- In a graph, **OK** starts a trace cursor (**LEFT/RIGHT** move it,
  **UP/DOWN** pick the function) and **TOOLBOX** finds the next zero,
  minimum, maximum or intersection to the right of it.
- In text fields (shell, editor, Python), **ALPHA** types the letters
  printed on the keys; SHIFT+ALPHA types capitals. **SHIFT** alone
  types `[ ] { } = _ < > #` on `( ) × ÷ + − . 0 ,`.
- In Python, a line that opens a block (`def`, `for`, `if`, ...)
  continues on the next line, indented for you; an empty line runs the
  block. **UP** recalls the previous line.
- In a running Python script, **BACK** raises `KeyboardInterrupt`.
- **ON/OFF** turns the screen and backlight off (and so does 5 minutes
  without a key press); ON/OFF turns them back on. While it's off the
  processor slows down to save the battery, unless USB is connected.
- The home screen shows the battery level, with a bolt while charging;
  with USB power the LED is orange while charging and green when full.
- After a crash the screen shows the fault and its address; any key
  restarts. The same report goes to the debug UART. If the calculator
  hangs, the watchdog restarts it after about 8 seconds.
- On the first start (or if the file storage is damaged) the calculator
  asks whether to format the storage: **OK** formats, **BACK** goes on
  without files.

Files are kept in the last 256 KB of the external flash, in two halves;
tidying up writes the other half first, so a power cut can't lose
files that were already saved. Settings (LED, brightness, language)
and the games' best scores are kept in a small file, `.settings`.

## Home screen

```
┌──────────────┬──────────────┬──────────────┐
│ Rekenmachine │  Functies    │ Vergelijking │
├──────────────┼──────────────┼──────────────┤
│  Statistiek  │   Python     │  Bestanden   │
├──────────────┼──────────────┼──────────────┤
│   Editor     │    Shell     │   Spellen    │
├──────────────┼──────────────┼──────────────┤
│    Docs      │ Instellingen │              │
└──────────────┴──────────────┴──────────────┘
```

## Source layout

```
numworks-os/
├── Makefile
├── main.c                      Boot sequence
├── linker/
│   └── numworks_n0120.ld       Linker script (QSPI XIP @ 0x90000000)
├── loader/                     Internal-flash loader: sets up the QSPI
│                               flash and starts the OS (or recovery)
├── include/
│   ├── config.h                Central configuration
│   └── stm32f730.h             Register definitions
├── bootloader/
│   ├── startup_stm32f730.s     Vector table + Reset_Handler
│   └── boot.c                  MPU, I-cache, SysTick, cycle counter
├── kernel/                     Event loop, scheduler, sleep
├── hal/                        LCD, backlight, keyboard, UART, LED,
│                               battery, clocks (192 MHz / 16 MHz),
│                               crash screen, newlib stubs
├── fs/
│   ├── flashfs.c/h             File system (append-only log, two areas)
│   └── storage_qspi.c          QSPI flash driver (AT25SF641), runs from RAM
├── shell/                      Terminal UI + commands
├── ui/
│   ├── filemanager.c           File manager
│   ├── lang.c/h                Interface language (Dutch / English)
│   └── line_input.c            Blocking line input (Python's input())
├── usb/
│   ├── usb_cdc.c/h             PC transfer protocol
│   └── usb_device.c/h          USB device stack (CDC-ACM serial port)
├── micropython-port/
│   ├── mp_port.c/h             MicroPython glue
│   ├── mpconfigport.h          MicroPython configuration
│   ├── micropython_embed.mk    Used by `make mp`
│   ├── shared/readline/        input() hook (replaces MicroPython's)
│   └── modules/nwos/           `display`, `kandinsky`, `ion`, `time`,
│                               `random`, `os`, open()
├── apps/
│   ├── common/expr.c/h         Expression evaluator (math apps)
│   ├── common/analysis.c/h     Zeros and extremes (graph analysis)
│   ├── common/stats.c/h        Statistics and linear regression
│   ├── settings/prefs.c/h      Settings kept in `.settings`
│   ├── games/                  Games menu, Snake, 2048
│   └── <app>/                  One directory per app
├── tools/
│   ├── web/uploader.html       PC file transfer from Chrome or Edge
│   ├── upload.py               PC file transfer, command line
│   └── transfer.py             Same, alternative command line
├── tests/                      Host tests (`make test`), see tests/README.md
└── docs/
    ├── BUILD.md                Building, flashing, recovery
    └── ARCHITECTURE.md         How the system works
```

## Building

See `docs/BUILD.md` for details (and for macOS). In short, on
Ubuntu or Debian, the firmware without Python:

```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi
make
```

With Python:

```bash
git clone https://github.com/micropython/micropython
make mp && make
```

The image is linked to run from the external QSPI flash at
`0x90000000`, started by the loader (`make loader`) in the internal
flash. `make flash` and `make flash-loader` ask for confirmation
because they replace NumWorks' firmware; `make phi`, `make delta` and
`make openocd` refuse to flash the image to an address it isn't linked
for.

`make test` builds and runs the host tests: the OS code against
simulated hardware, with the address and undefined-behaviour sanitizers
(see `tests/README.md`).

## Code design

- Every app is a module in `apps/<name>/` with `init()`, `redraw()` and
  `handle_event()`; apps that work between key presses also have a
  `tick()` (Tetris, Shell).
- All drawing goes into a framebuffer through `display_*()`; the kernel
  sends the changed rectangle to the LCD (ST7789V).
- `kernel_set_app()` switches apps. Tasks sleep between ticks and the
  CPU waits in `WFI` when idle.

## License

The code in this repository is MIT licensed (see `LICENSE`).
MicroPython (`make mp`) keeps its own license (MIT); the `random`
module is adapted from MicroPython's.

Early versions of this repository contained a dump of NumWorks'
firmware (`epsilon-qspi-backup.bin`). It isn't ours to distribute, and
it couldn't restore a calculator anyway, so it was removed and the
repository's history was rewritten without it. If you cloned before
that, delete your clone and clone again.
