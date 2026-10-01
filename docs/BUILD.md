# Building & Flashing NumWorks OS

> This firmware targets the **STM32F730** (NumWorks N0110 family). It does
> **not** run on the N0120, which has an STM32H7. Nothing has been tested
> on real hardware yet.

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi \
                 dfu-util openocd python3

# macOS: the compiler, plus newlib (C library and headers)
brew install --cask gcc-arm-embedded
brew install dfu-util openocd

# PC file transfer tools
pip install pyserial
```

If the build can't find `libc.a`, run `make print-libs`.

## Build

```bash
make -j4
# Output: build/numworks_os_n0120.bin (and .elf, .map)
```

Without MicroPython the Python app and `run` say Python isn't available;
everything else works.

## MicroPython (optional)

MicroPython is built from its *embed* port: `make mp` generates
`micropython-port/micropython_embed/` (MicroPython's core plus headers
generated for `micropython-port/mpconfigport.h` and our `display`
module). The next `make` compiles it in.

```bash
git clone https://github.com/micropython/micropython
make mp            # or: make mp MICROPYTHON=/path/to/micropython
make -j4
```

Run `make mp` again after changing `mpconfigport.h` or the modules in
`micropython-port/modules/`, and after updating this repository (if
either changed). `make distclean` removes the generated package.

## FatFs (optional, for USB drives)

`fs/ff.c` is a stub. To use the real FatFs:

1. Download FatFs (R0.15 or later) from http://elm-chan.org/fsw/ff/.
2. Copy `ff.h`, `ff.c` and `ffconf.h` into `fs/` (keep our `diskio.c/h`).
3. In `fs/ffconf.h`, set `FF_VOLUMES` to `2`: the USB drive is `"1:"`.

USB drives still won't mount: there is no USB host mass-storage driver
yet, and the calculator can't power a drive (see *USB notes*).

## Flashing and recovery

**Read this before flashing.**

- The image is linked to execute from the external QSPI flash at
  `0x90000000` (`linker/numworks_n0120.ld`).
- `make flash` writes it there in rescue mode, **over the stock
  firmware**. It asks you to confirm:

  ```bash
  make flash CONFIRM=overwrite-stock-firmware
  ```

- `make phi`, `make delta` and `make openocd` write to internal-flash
  addresses (`0x08040000` / `0x08010000`). They refuse to flash an image
  linked for another address, because it would not run there. Use them
  only after switching `LDSCRIPT` to a matching linker script. Phi
  (https://github.com/M4xi1m3/nwphi) was written for the N0110.

### Restore Official Firmware

Don't use `make restore` or `epsilon-qspi-backup.bin`. The first 108 KB
of that dump (0x0–0x1AFFF) is an old build of this OS, not the Epsilon
kernel, so it can't boot. Don't write `epsilon.bin` to `0x08000000`
either: that is internal flash, where NumWorks' bootloader lives;
Epsilon itself lives in the external flash.

Use NumWorks' own recovery instead, which reinstalls a complete, signed
firmware:

1. Put the calculator in rescue mode (hold 6, press RESET). The screen
   shows `numworks.com/rescue`.
2. Open that address in a desktop browser with the calculator connected
   over USB, and follow the instructions there. If the calculator still
   boots, updating from https://my.numworks.com/devices/upgrade works too.

## USB notes

- **PC file transfer** needs a USB device (CDC-ACM) stack, which isn't
  written yet. The transfer protocol and the PC tools are done and
  tested against each other.
- **USB drives** can't work as the board is: nothing on it can supply
  5 V on VBUS (the RT9526A is a charger, the USBLC6-2 is ESD
  protection). USB host mode is therefore not started at boot.

## PC file transfer

Once the USB stack exists:

```bash
python tools/upload.py --port /dev/ttyACM0 list
python tools/upload.py --port /dev/ttyACM0 upload hello.py
python tools/upload.py --port /dev/ttyACM0 download notes.txt
python tools/upload.py --port /dev/ttyACM0 delete notes.txt
```

`tools/transfer.py` does the same with a slightly different command
line (`upload <local> [<name on calculator>]`). Files are at most 8 KB,
and names at most 23 characters without spaces.
