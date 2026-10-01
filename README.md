# NumWorks OS — custom calculator firmware

A custom firmware for NumWorks calculators: a graphical home screen,
a calculator, graphing, equation solving, MicroPython, a shell, a file
manager, a text editor and Tetris.

> **Status — read first.**
> - The code targets the **STM32F730** (the NumWorks N0110 family). The
>   **NumWorks N0120 uses an STM32H7**, so this firmware does **not** run
>   on an N0120 yet; that needs a port based on the N0120 schematic.
> - The keyboard and LCD pin mappings have not been checked against a
>   schematic, and the USB device stack (for PC file transfer) is not
>   written yet.
> - Nothing here has been tested on a real calculator. Before flashing
>   anything, read *Flashing and recovery* in `docs/BUILD.md`.

## Features

| App | Dutch name | What it does | State |
|-----|-----------|--------------|-------|
| **Rekenmachine** | Calculator | Scientific calculator, `Ans`, inverse functions | Works (native evaluator) |
| **Functies** | Functions | Up to 4 functions: graph (pan/zoom), table; edit/delete | Works |
| **Vergelijkingen** | Equations | Quadratic, 2×2 linear system, f(x)=0 (Newton) | Works |
| **Python** | Python REPL | MicroPython: `math`, `display`, read-only `open()` | Needs `make mp` |
| **Bestanden** | File manager | Open in editor, new file, delete (SHIFT twice) | Works |
| **Shell** | Shell | `ls cat touch rm echo run mem fm reboot`; also over UART | Works |
| **Tetris** | Tetris | Classic Tetris | Works |
| **Docs** | Docs | Built-in reference | Works |
| **Instellingen** | Settings | LED, version, reboot | LED pin unverified, "white" = red |
| **Foto's** | Photo viewer | 24-bit BMP from a USB drive | Needs USB host + FatFs (missing) |
| **Editor** | Text editor | Edit files up to 4 KB, asks a name for new files | Works |
| PC transfer | `tools/upload.py` | List, upload, download, delete files | Protocol done, USB stack missing |

## Keys

Navigate with the arrow keys, **OK** (or **EXE**) opens or confirms,
**HOME**/**BACK** go back. Arrows and backspace repeat when held.

- In the math apps, **SHIFT** gives the inverse function
  (sin → asin, ln → e^x, √ → ∛).
- In text fields (shell, editor, Python), **ALPHA** types the letters
  printed on the keys; SHIFT+ALPHA types capitals. **SHIFT** alone
  types `[ ] { } = _ < > #` on `( ) × ÷ + − . 0 ,`.
- In a running Python script, **BACK** raises `KeyboardInterrupt`.

## Home screen

```
┌──────────────┬──────────────┬──────────────┐
│ Rekenmachine │  Functies    │ Vergelijking │
├──────────────┼──────────────┼──────────────┤
│   Python     │  Bestanden   │    Shell     │
├──────────────┼──────────────┼──────────────┤
│   Tetris     │    Docs      │ Instellingen │
├──────────────┼──────────────┼──────────────┤
│   Foto's     │   Editor     │              │
└──────────────┴──────────────┴──────────────┘
```

## Source layout

```
numworks-os/
├── Makefile
├── main.c                      Boot sequence
├── linker/
│   ├── numworks_n0120.ld       Linker script (QSPI XIP @ 0x90000000)
│   └── numworks.ld             Legacy, unused (wrong memory map)
├── include/
│   ├── config.h                Central configuration
│   └── stm32f730.h             Register definitions
├── bootloader/
│   ├── startup_stm32f730.s     Vector table + Reset_Handler
│   └── boot.c                  Clocks (216 MHz), MPU, I-cache, SysTick
├── kernel/                     Event loop, scheduler, pool allocator
├── hal/                        LCD, keyboard, UART, timer
├── fs/
│   ├── flashfs.c/h             Flash file system (append-only log)
│   ├── ff.c / ff.h             FatFs stub (replace with real FatFs)
│   └── diskio.c/h              FatFs drive glue (drive 1 = USB)
├── shell/                      Terminal UI + commands
├── ui/filemanager.c            File manager
├── usb/
│   ├── usb_cdc.c/h             PC transfer protocol (USB stack missing)
│   └── usb_host.c/h            USB host skeleton (not started at boot)
├── micropython-port/
│   ├── mp_port.c/h             MicroPython glue
│   ├── mpconfigport.h          MicroPython configuration
│   ├── micropython_embed.mk    Used by `make mp`
│   └── modules/nwos/           `display` module, builtin open()
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
  pushes it to the LCD when something changed.
- `kernel_set_app()` switches apps. Tasks sleep between ticks and the
  CPU waits in `WFI` when idle.

## License

The code in this repository is MIT licensed. MicroPython (`make mp`)
and FatFs keep their own licenses. `epsilon-qspi-backup.bin` is a dump
of NumWorks' own firmware: it is not covered by this repository's
license.
