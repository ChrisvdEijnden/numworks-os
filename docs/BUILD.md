# Building & Flashing NumWorks OS N0120

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi openocd dfu-util

# macOS
brew install --cask gcc-arm-embedded
brew install dfu-util openocd

# Python tools
pip install pyserial
```

## Get MicroPython

```bash
git clone https://github.com/micropython/micropython
make -C micropython/mpy-cross           # Build cross-compiler first
cp -r micropython/py ./micropython-port/py
cp -r micropython/lib ./micropython-port/lib
make mp                                  # Build static library
```

## Get FatFs (for USB host FAT32)

Download from http://elm-chan.org/fsw/ff/ and copy ff.h, ff.c, ffconf.h into fs/.
The diskio.c in this project provides the USB MSC glue layer.

## Build

```bash
make -j4
# Output: build/numworks_os_n0120.bin
```

## Flash

### Phi Bootloader (Recommended for N0120)

1. Install Phi on your calculator (https://github.com/M4xi1m3/nwphi)
2. Enter DFU mode: hold RESET + 6, release RESET first
3. Flash:

```bash
make phi
# or:
dfu-util -d 0483:df11 -a 0 -s 0x08040000:leave -D build/numworks_os_n0120.bin
```

### Delta Bootloader

```bash
make delta
```

### OpenOCD + ST-Link (Dev, bypasses signature)

```bash
make openocd
```

## Restore Official Firmware

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

## USB Drive Notes

The N0120 does not output 5V VBUS. Use a self-powered USB hub or OTG adapter.
Supported: .py, .txt, .bmp, .jpg, .png files on FAT32 drives.

## PC File Transfer

```bash
python tools/upload.py --port /dev/ttyACM0 list
python tools/upload.py --port /dev/ttyACM0 upload hello.py
python tools/upload.py --port /dev/ttyACM0 download notes.txt
```
