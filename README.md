# NumWorks OS — N0120 Custom Firmware

A fully custom firmware for the **NumWorks N0120** calculator, built on top of the existing NumWorks OS base (from the provided backup archive). Adds a graphical homepage, new apps, USB drive support, and LED control.

## Features

| App | Dutch Name | Description |
|-----|-----------|-------------|
| **Rekenmachine** | Calculator | Scientific calculator (native evaluator, no Python needed) |
| **Functies** | Functions | Graph plotter, table view, zoom/pan |
| **Vergelijkingen** | Equations | Quadratic, linear systems, single equation |
| **Python** | Python REPL | Interactive MicroPython REPL |
| **Bestanden** | File Manager | Browse & manage internal flash files |
| **Shell** | Shell | Unix-like terminal (ls, cat, rm, run, ...) |
| **Tetris** | Tetris | Classic Tetris game |
| **Docs** | Docs | Built-in reference documentation |
| **Instellingen** | Settings | LED lamp (rood/wit/uit), version info, reboot |
| **Foto's** | Photo Viewer | View .bmp/.jpg/.png from USB drive |
| **Editor** | Text Editor | Open (from Bestanden), edit, save .txt/.py files |

## Homepage

3×4 icon grid with colour indicators. Navigate with arrow keys, press OK (or EXE) to open.
In the math apps, SHIFT 9 / SHIFT 0 type `(` / `)` and SHIFT gives the inverse functions.

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

## Source Layout

```
epsilon-nwos/
├── Makefile
├── README.md
├── main.c                      Boot entry point
├── linker/
│   ├── numworks_n0120.ld       Linker script (Phi @ 0x08040000)
│   └── numworks.ld             Original linker script
├── include/
│   ├── config.h                Central config (updated for N0120)
│   └── stm32f730.h             Register definitions
├── bootloader/
│   ├── startup_stm32f730.s     Vector table + Reset_Handler
│   └── boot.c                  PLL init, SysTick
├── kernel/
│   ├── kernel.c/h              Event loop + app dispatcher (extended)
│   ├── scheduler.c/h           Cooperative scheduler
│   └── memory.c/h              Memory pool allocator
├── hal/
│   ├── display.c/h             ILI9341 FSMC framebuffer driver
│   ├── keyboard.c/h            9×6 GPIO matrix scanner
│   ├── uart.c/h                USART1 debug
│   ├── timer.c/h               TIM6 microsecond counter
│   └── font.c/h                6×8 bitmap font
├── fs/
│   ├── flashfs.c/h             Internal flash filesystem (sector 7)
│   ├── ff.c / ff.h             Chan FatFs (for USB drive)
│   └── diskio.c                FatFs ↔ USB MSC glue
├── shell/
│   ├── shell.c/h               Terminal UI (reused from backup)
│   └── commands.c/h            Shell commands
├── ui/
│   ├── filemanager.c/h         Graphical file browser (reused)
│   └── font.c                  Font bitmap data
├── usb/
│   ├── usb_cdc.c/h             USB CDC-ACM virtual serial (reused)
│   └── usb_host.c/h            USB OTG Host for mass storage (NEW)
├── micropython-port/
│   ├── mp_port.c/h             MicroPython glue + output capture
│   └── mpconfigport.h          Build config
├── apps/                       ← All NEW apps
│   ├── home/                   Icon grid homepage
│   ├── calculator/             Scientific calculator
│   ├── functions/              Graph plotter
│   ├── equations/              Equation solver
│   ├── python_app/             Python REPL
│   ├── tetris/                 Tetris game
│   ├── docs_app/               Built-in docs
│   ├── settings/               Settings + LED control
│   ├── photo_viewer/           Image viewer (USB)
│   └── text_editor/            Text/Python editor
├── tools/
│   ├── upload.py               PC file transfer
│   └── transfer.py             Transfer utility
└── docs/
    ├── BUILD.md                Build & flash instructions
    └── ARCHITECTURE.md         System architecture reference
```

## Building

See `docs/BUILD.md` for full instructions. Quick start:

```bash
# 1. Get toolchain
sudo apt install gcc-arm-none-eabi dfu-util

# 2. Get MicroPython
git clone https://github.com/micropython/micropython
make -C micropython/mpy-cross
make mp

# 3. Build
make -j4

# 4. Flash (Phi bootloader)
make phi
```

## Bootloader Compatibility

This firmware is designed for the **Phi bootloader** on N0120 (firmware at 0x08040000).

To use **Delta** or **OpenOCD (no bootloader)**:
- Change `FIRMWARE_START` in `linker/numworks_n0120.ld`
- For Delta: `0x08010000`
- For full flash: `0x08000000`

## Code Design

- Every app is a self-contained module in `apps/<name>/`
- Apps implement three functions: `init()`, `redraw()`, `handle_event()`
- All drawing goes through `display_*()` functions (framebuffer + flush)
- The kernel `kernel_set_app()` function switches between apps
- No direct hardware access from app code — HAL functions only

## License

MIT. NumWorks hardware schematics from the open-source NumWorks project.
