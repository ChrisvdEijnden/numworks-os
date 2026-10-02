# Building & Flashing NumWorks OS

> This firmware targets the **STM32F730** (NumWorks N0110 family). It does
> **not** run on the N0120, which has an STM32H7. Nothing has been tested
> on real hardware yet. It starts through its own small loader, which
> replaces NumWorks' code in the internal flash: read *Boot chain* and
> *Installing* below first.

## Prerequisites

Ubuntu or Debian:

```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi dfu-util openocd python3
pip install pyserial
```

macOS (the cask is Arm's toolchain, which includes the C library,
newlib):

```bash
brew install --cask gcc-arm-embedded
brew install dfu-util openocd
pip install pyserial
```

If Homebrew's `arm-none-eabi-gcc` formula is installed too, its
compiler comes first and Arm's isn't linked ("skipping link"). Put
Arm's first in your PATH (adjust the version to the one installed):

```bash
echo 'export PATH="/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
arm-none-eabi-gcc --version
```

The last line should mention "Arm GNU Toolchain". If the build can't
find `libc.a`, run `make print-libs`.

The command blocks in these docs have no comments in them, so they can
be pasted into zsh, which by default doesn't treat `#` as a comment.

## Build

```bash
make -j4
```

The output is `build/numworks_os_n0120.bin` (and `.elf`, `.map`).

Without MicroPython the Python app and `run` say Python isn't available;
everything else works.

## MicroPython (optional)

MicroPython is built from its *embed* port: `make mp` generates
`micropython-port/micropython_embed/` (MicroPython's core plus headers
generated for `micropython-port/mpconfigport.h` and our `display`
module). The next `make` compiles it in. With a MicroPython checkout
elsewhere, use `make mp MICROPYTHON=/path/to/micropython`.

```bash
git clone https://github.com/micropython/micropython
make mp
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

Either way, NumWorks' code in the internal flash doesn't start this OS:
it jumps somewhere into our image, not to our reset handler. So the OS
comes with its own loader for the internal flash (`loader/`, about
1.4 KB, `make loader`). At reset it:

1. reads key 6 (ten times, 100 µs apart): held, it starts ST's
   bootloader (below);
2. sets up the QSPI flash the way Epsilon does: pins PB2, PB6, PC9,
   PD12, PD13, PE2; clock HCLK/2; 8 MB; chip select high for at least
   50 ns; clock mode 3. A reset of the STM32 doesn't reset the flash, so
   the loader first takes it out of continuous-read mode and deep
   power-down and waits while it is busy (an erase cut short by the
   reset). It checks the JEDEC ID, sets the quad-enable bit if needed,
   and maps the flash at `0x90000000` with Fast Read Quad I/O (EBh,
   continuous read), or with one-line Fast Read (0Bh) if the bit can't
   be set;
3. checks the vector table at `0x90000000`: the stack pointer in RAM,
   the reset handler a Thumb address in the code area (below the file
   system);
4. sets VTOR and the stack pointer and jumps to the reset handler.

If anything fails (no answer from the flash, no valid image) it starts
ST's bootloader with the red LED on, rather than crashing. The loader
runs at the 16 MHz the chip resets with and uses only its stack; the
OS sets up the clocks itself. The QSPI settings it leaves
(`loader/loader_qspi.h`) are what the OS's storage driver expects.
`tests/loader` runs the loader in an emulated Cortex-M7 with models of
the QSPI controller and the AT25SF641, and `tests/qspi` runs the
storage driver on the settings the loader leaves.

**ST's bootloader** is in the STM32's ROM and can't be overwritten. It
shows up over USB as "STM32 BOOTLOADER" (DFU, `0483:df11`) and can read
and write the internal flash, not the QSPI flash. Hold 6, press RESET,
release 6 to get there: NumWorks' own flashing scripts for the N0110
(Epsilon 15) use that. The loader also checks key 6 itself, in case.
When the loader starts ST's bootloader, the screen stays dark and the
LED is red.

## Installing

Not tried on a real calculator yet. It needs a calculator running
Epsilon 15 or older: whether NumWorks' bootloader in Epsilon 16 and
later protects the internal flash from being written is unknown.

1. **Save the internal flash.** Hold 6, press RESET, release 6, then
   save it to `backup-internal.bin`:

   ```bash
   make backup-internal
   ```

   Keep `backup-internal.bin` (git ignores it). It is NumWorks' code:
   it isn't ours to share.
2. **Write the OS to the QSPI flash** with Epsilon's own DFU: press
   RESET, let Epsilon start and connect the calculator over USB; it then
   shows up as `0483:a291`.

   ```bash
   make flash CONFIRM=overwrite-stock-firmware
   ```

   The calculator doesn't start after this: Epsilon is gone, and its
   start-up code in the internal flash doesn't know our image.
3. **Write the loader**: hold 6, press RESET, release 6, then

   ```bash
   make flash-loader CONFIRM=replace-internal-flash
   ```

   When it is done, the calculator restarts into the OS.

**Updating the OS later.** ST's bootloader can't write the QSPI flash,
and Epsilon's DFU is gone. Ways in, until this OS has a USB update mode
of its own:

- NumWorks' RAM flasher: build `flasher.light` from Epsilon 15's source
  and load it into RAM through ST's bootloader, the way that version's
  `build/targets.device.n0110.mak` does. It then shows up as
  `0483:a291`, and `make flash` works. It is NumWorks' code: build it
  yourself, don't pass it around.
- An SWD probe (ST-Link) with OpenOCD's `stmqspi` flash driver.

**Going back to NumWorks' firmware**: hold 6, press RESET, release 6,
run `make restore-internal` (writes `backup-internal.bin` back), then
use NumWorks' recovery (*Restore Official Firmware* below).

## Flashing and recovery

**Read this before flashing.**

- The image is linked to execute from the external QSPI flash at
  `0x90000000` (`linker/numworks_n0120.ld`).
- The last 256 KB of the QSPI flash (from `0x907C0000`) hold the
  file system; the linker refuses images that would overlap it. On the
  first start the calculator asks before formatting it.
- `make flash` writes it there through NumWorks' DFU, **over the stock
  firmware**; it boots only with the loader in the internal flash (see
  *Installing*). It asks you to confirm:

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
firmware. If this OS's loader is in the internal flash, put NumWorks'
code back first (`make restore-internal`, see *Installing*).

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
