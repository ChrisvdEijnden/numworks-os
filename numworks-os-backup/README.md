# NumWorks OS

A minimal, from-scratch embedded operating system for the **NumWorks N0110 calculator**, replacing the default firmware with a lightweight kernel, terminal shell, Python runtime, and graphical file manager.

> **Target:** STM32F730R8T6 · 216 MHz Cortex-M7 · 512 KB Flash · 256 KB RAM

---

## Features

| Feature | Detail |
|---------|--------|
| **Bootloader** | PLL @ 216 MHz, FPU enable, <4 KB |
| **Kernel** | Cooperative event-loop scheduler, 4 tasks |
| **Shell** | 11 Unix-like commands on the LCD keypad |
| **Filesystem** | Internal flash FS (32 files, 60 KB pool) |
| **Python** | MicroPython with custom `display` module |
| **File Manager** | Graphical navigator with open/delete/rename |
| **USB** | CDC-ACM virtual serial for PC file transfer |

---

## Quick Start

### 1. Prerequisites

```bash
# macOS
brew install arm-none-eabi-gcc openocd

# Ubuntu / Debian
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi openocd

# Python tool dependencies
pip install pyserial
```

### 2. Get MicroPython source

```bash
git clone https://github.com/micropython/micropython
cp -r micropython/py ./micropython-port/py
cp -r micropython/lib ./micropython-port/lib
# Build the cross-compiler first
make -C micropython/mpy-cross
```

### 3. Get FatFs (optional, for FAT32 overlay)

```bash
# Download from http://elm-chan.org/fsw/ff/
# Copy ff.h, ff.c, ffconf.h into fs/
```

### 4. Build

```bash
make -j4
```

Output: `build/numworks_os.bin`

Expected sizes:
```
   text    data     bss     dec
 141312     512  162304  304128   ~297 KB total
```

### 5. Flash

**Via ST-Link (recommended for development):**
```bash
make flash
```

**Via DFU (USB bootloader — NumWorks recovery mode):**
1. Hold `6` + `Reset` to enter DFU mode
2. Run:
```bash
make dfu
```

**Via OpenOCD manually:**
```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "program build/numworks_os.bin 0x08000000 verify reset exit"
```

### 6. Transfer files from PC

```bash
# List files on calculator
python tools/upload.py --port /dev/ttyACM0 list

# Upload a Python script
python tools/upload.py --port /dev/ttyACM0 upload hello.py

# Download a file
python tools/upload.py --port /dev/ttyACM0 download notes.txt

# Delete a file
python tools/upload.py --port /dev/ttyACM0 delete hello.py
```

---

## Using the Shell

The shell appears at boot. The keypad works as follows:

| Key | Action |
|-----|--------|
| `ALPHA` | Toggle letter input mode |
| `SHIFT` | Toggle uppercase |
| `EXE` | Execute command / confirm |
| `BACKSPACE` | Delete last character |
| `↑ / ↓` | Scroll command history |
| `HOME` | Return to shell from file manager |

### Example session

```
NumWorks OS v0.1  Ready.
> ls
  hello.py         512 B
  notes.txt       1024 B
  2 KB used, 62 KB free

> cat hello.py
import display
display.fill(display.BLACK)
display.str(60, 100, "Hello World!", display.WHITE)
display.flush()

> run hello.py
Running: hello.py

> mem
RAM heap:  1280 B used, 6912 B free
Flash FS:  1536 B used, 63488 B free

> fm
[Opens graphical file manager]
```

---

## File Manager Controls

| Key | Action |
|-----|--------|
| `↑ / ↓` | Navigate files |
| `EXE` | Open file (cat) or run .py script |
| `SHIFT` | Delete selected file |
| `ALPHA` | Rename (prompts in shell) |
| `BACK / HOME` | Return to shell |

---

## Python Example Scripts

**hello.py** — Display hello world:
```python
import display
display.fill(display.BLACK)
display.str(60, 110, "Hello, NumWorks!", display.WHITE)
display.flush()
```

**counter.py** — Simple counter:
```python
import display, utime
for i in range(100):
    display.fill(display.BLACK)
    display.str(130, 110, str(i), display.GREEN)
    display.flush()
    utime.sleep_ms(100)
```

---

## Source Layout

```
numworks-os/
├── Makefile
├── README.md
├── linker/
│   └── numworks.ld          Linker script (Flash + RAM layout)
├── include/
│   ├── stm32f730.h          Peripheral register definitions
│   └── config.h             Central configuration (clock, sizes)
├── bootloader/
│   ├── startup_stm32f730.s  Vector table, Reset_Handler
│   └── boot.c               PLL init, SysTick, GPIO clocks
├── kernel/
│   ├── kernel.c/h           Main loop, event queue, app switching
│   ├── scheduler.c/h        Cooperative round-robin scheduler
│   └── memory.c/h           64-byte block pool allocator
├── hal/
│   ├── hal.c/h              HAL init + timing
│   ├── display.c/h          ILI9341 FSMC driver + framebuffer
│   ├── keyboard.c/h         9×6 GPIO matrix scanner
│   ├── uart.c/h             USART1 debug/shell interface
│   ├── timer.c/h            TIM6 microsecond counter
│   ├── font.h               6×8 font interface
│   └── clocks.c             (placeholder)
├── fs/
│   ├── flashfs.c/h          Internal flash filesystem
│   ├── ff.c / ff.h          FatFs (replace with Chan FatFs)
│   └── diskio.c             FatFs ↔ flashfs glue
├── shell/
│   ├── shell.c/h            Terminal UI + keypad input
│   └── commands.c/h         ls, cat, rm, run, mem, fm, …
├── ui/
│   ├── filemanager.c/h      Graphical file browser
│   └── font.c               6×8 bitmap font data (760 B)
├── usb/
│   └── usb_cdc.c/h          USB CDC-ACM virtual serial port
├── micropython-port/
│   └── mp_port.c/h          MicroPython platform glue
├── tools/
│   └── upload.py            PC file transfer utility
└── docs/
    └── ARCHITECTURE.md      Full architecture reference
```

---

## Restoring Default Firmware

If you want to restore the original NumWorks firmware:

```bash
# Download from https://my.numworks.com/devices/upgrade
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D epsilon.bin
```

Or use the official NumWorks recovery tool at https://my.numworks.com/devices/upgrade.

---

## License

MIT — see LICENSE. NumWorks hardware schematics referenced from the open-source NumWorks firmware project.
