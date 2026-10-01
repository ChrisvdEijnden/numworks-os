# Building & Flashing NumWorks OS

> This firmware targets the **STM32F730** (NumWorks N0110 family). It does
> **not** run on the N0120, which has an STM32H7. Nothing has been tested
> on real hardware yet, and it can't start on its own yet: see
> *Boot chain* below.

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
either changed; this version added the `os` module, so run it once).
`make distclean` removes the generated package.

The C code is compiled against newlib's own headers (`-std=gnu11`);
`make print-libs` shows which `libc.a` the build found.

## Boot chain

How the N0110 starts, from the firmware NumWorks published:

- **Epsilon up to version 15**: the internal flash (64 KB at
  `0x08000000`) holds Epsilon's own start-up code. It sets up the clocks
  and the QSPI flash, then calls a function in the external flash at an
  address fixed when *that* Epsilon was built, not a vector table.
- **Epsilon 16 and later**: the internal flash holds NumWorks' bootloader,
  which only starts NumWorks' signed kernel. Third-party software runs as
  a "userland" on top of that kernel; this OS is a whole kernel, so it
  can't run that way.

Either way, writing this image to `0x90000000` alone gives a calculator
that doesn't start: the code in the internal flash jumps somewhere into
our image, not to our reset handler. What's missing is a small loader in
the internal flash that sets up the QSPI flash in memory-mapped mode
(pins PB2, PB6, PC9, PD12, PD13, PE2; see `include/config.h`) and jumps
to the vector table at `0x90000000`. That loader isn't written yet.
Replacing the internal flash also removes NumWorks' own code, so before
doing that, check how your calculator's recovery mode works. Recovering
from a bad internal flash may need the STM32's built-in bootloader or an
SWD probe.

## Flashing and recovery

**Read this before flashing.**

- The image is linked to execute from the external QSPI flash at
  `0x90000000` (`linker/numworks_n0120.ld`).
- The last 256 KB of the QSPI flash (from `0x907C0000`) hold the
  file system; the linker refuses images that would overlap it. On the
  first start the calculator asks before formatting it.
- `make flash` writes it there in rescue mode, **over the stock
  firmware**; on its own that doesn't boot (see *Boot chain*). It asks
  you to confirm:

  ```bash
  make flash CONFIRM=overwrite-stock-firmware
  ```

- `make phi`, `make delta` and `make openocd` write to internal-flash
  addresses (`0x08040000` / `0x08010000`). They refuse to flash an image
  linked for another address, because it would not run there. Use them
  only after switching `LDSCRIPT` to a matching linker script. Phi
  (https://github.com/M4xi1m3/nwphi) was written for the N0110.
- If the screen stays black, the UART log shows the display ID that was
  read (`85 85 52` for an ST7789V). If the picture is mirrored, or red
  and blue are swapped, change `LCD_MADCTL` in `include/config.h`.
- The watchdog restarts the calculator 8 s after a hang. Set
  `WATCHDOG_ENABLED` to 0 when stepping through code without a
  debugger that freezes it.

### Restore Official Firmware

If you have the `epsilon-qspi-backup.bin` that early clones of this
repository contained, don't use it: its first 108 KB (0x0–0x1AFFF) are
an old build of this OS, not the Epsilon kernel, so it can't boot. Don't
write `epsilon.bin` to `0x08000000` either: that is internal flash,
where NumWorks' bootloader lives; Epsilon itself lives in the external
flash.

Use NumWorks' own recovery instead, which reinstalls a complete, signed
firmware:

1. Put the calculator in rescue mode (hold 6, press RESET). The screen
   shows `numworks.com/rescue`.
2. Open that address in a desktop browser with the calculator connected
   over USB, and follow the instructions there. If the calculator still
   boots, updating from https://my.numworks.com/devices/upgrade works too.

## Debug UART

The boot log, crash reports and a shell are on USART6: TX on PC6, RX on
PC7, 115200 8N1, 3.3 V levels. Connect a 3.3 V USB-serial adapter there
(not 5 V).

## USB notes

- **PC file transfer**: the calculator is a CDC-ACM serial port (no
  driver needed on Linux, macOS or Windows 10+), VID:PID 1209:0001. The
  stack and the protocol have been tested together on a simulated USB
  core, not on a real one yet. 1209:0001 is a pid.codes test ID: request
  a product ID of our own before distributing builds.
- **USB drives** aren't supported: nothing on the board can supply 5 V
  on VBUS (the RT9526A is a charger, the USBLC6-2 is ESD protection),
  so the USB port only works as a device. Earlier versions had a photo
  viewer for USB drives and a FatFs stub; both were removed.

## PC file transfer

**From the browser** (nothing to install): open `tools/web/uploader.html`
in Chrome or Edge on a computer — double-click it, or host it, for
example with GitHub Pages — click *Connect*, pick the calculator, then
drag files onto the page. The page lists, uploads, downloads and
deletes files, in Dutch or English. Web Serial isn't available in
Firefox or Safari, or on phones.

**From the command line**, with the calculator connected over USB:

```bash
python tools/upload.py --port /dev/ttyACM0 list
python tools/upload.py --port /dev/ttyACM0 upload hello.py
python tools/upload.py --port /dev/ttyACM0 download notes.txt
python tools/upload.py --port /dev/ttyACM0 delete notes.txt
```

`tools/transfer.py` does the same with a slightly different command
line (`upload <local> [<name on calculator>]`). Files are at most 100 KB
(all files together about 112 KB), and names at most 23 characters
without spaces. Uploading over an existing file keeps the old one until
the new one is complete, unless both don't fit.

Statistics data can be prepared on the PC: upload a `stats.csv` with one
`x,y` pair per line (`y` may be left empty).
