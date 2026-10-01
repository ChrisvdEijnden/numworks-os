# NumWorks OS — custom calculator firmware

A custom firmware for NumWorks calculators: a graphical home screen,
a calculator, graphing, equation solving, MicroPython, a shell, a file
manager, a text editor and Tetris.

> **Status — read first.**
> - The code targets the **STM32F730** (the NumWorks N0110 family). The
>   **NumWorks N0120 uses an STM32H7**, so this firmware does **not** run
>   on an N0120 yet; that needs a port based on the N0120 schematic.
> - The board pins (keyboard, LCD, backlight, LED, battery, USB, debug
>   UART) follow the N0110 configuration NumWorks published with
>   Epsilon 15.5.
> - Nothing here has been tested on a real calculator, and it doesn't
>   boot by itself yet: NumWorks' code in the internal flash doesn't
>   jump to it (see *Boot chain* in `docs/BUILD.md`). Read
>   *Flashing and recovery* there before flashing anything.

## Features

| App | Dutch name | What it does | State |
|-----|-----------|--------------|-------|
| **Rekenmachine** | Calculator | Scientific calculator, `Ans`, inverse functions | Works (native evaluator) |
| **Functies** | Functions | Up to 4 functions: graph (pan/zoom), table; edit/delete | Works |
| **Vergelijkingen** | Equations | Quadratic, 2×2 linear system, f(x)=0 (Newton) | Works |
| **Python** | Python REPL | MicroPython: multi-line blocks, `input()`, import your own `.py` files, `math`, `time`, `random`, `os`, `display`, NumWorks' `kandinsky` and `ion`, `open()` to read and write files | Needs `make mp` |
| **Bestanden** | File manager | Open in editor, new file, delete (SHIFT twice) | Works |
| **Shell** | Shell | `ls cat touch rm echo run mem bat fm reboot`; also over UART | Works |
| **Tetris** | Tetris | Classic Tetris | Works |
| **Docs** | Docs | Built-in reference | Works |
| **Instellingen** | Settings | RGB LED, screen brightness, version, reboot | Works |
| **Editor** | Text editor | Files up to 8 KB, scrolls sideways for long lines, asks a name for new files | Works |
| PC transfer | `tools/upload.py` | List, upload, download, delete files over USB (a serial port on the PC) | Works in simulation |

## Keys

Navigate with the arrow keys, **OK** (or **EXE**) opens or confirms,
**HOME**/**BACK** go back. Arrows and backspace repeat when held.

- In the math apps, **SHIFT** gives the inverse function
  (sin → asin, ln → e^x, √ → ∛).
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
files that were already saved.

## Home screen

```
┌──────────────┬──────────────┬──────────────┐
│ Rekenmachine │  Functies    │ Vergelijking │
├──────────────┼──────────────┼──────────────┤
│   Python     │  Bestanden   │    Shell     │
├──────────────┼──────────────┼──────────────┤
│   Tetris     │    Docs      │ Instellingen │
├──────────────┼──────────────┼──────────────┤
│   Editor     │              │              │
└──────────────┴──────────────┴──────────────┘
```

## Source layout

```
numworks-os/
├── Makefile
├── main.c                      Boot sequence
├── linker/
│   └── numworks_n0120.ld       Linker script (QSPI XIP @ 0x90000000)
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
│   └── <app>/                  One directory per app
├── tools/
│   ├── upload.py               PC file transfer
│   └── transfer.py             Same, alternative command line
└── docs/
    ├── BUILD.md                Building, flashing, recovery
    └── ARCHITECTURE.md         How the system works
```

## Building

See `docs/BUILD.md` for details. In short:

```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi
make                    # firmware without Python

git clone https://github.com/micropython/micropython
make mp && make         # firmware with Python
```

The image is linked to run from the external QSPI flash at
`0x90000000`. `make phi`, `make delta` and `make openocd` refuse to
flash it to an address it isn't linked for, and `make flash` asks for
confirmation because it overwrites the stock firmware.

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

Earlier versions of this repository contained a dump of NumWorks'
firmware (`epsilon-qspi-backup.bin`). It was removed: it isn't ours to
distribute, and it couldn't restore a calculator anyway.
