# Host tests

The OS code runs here on a PC, against simulated hardware: flash chips,
the QSPI and USB controllers, the LCD bus, the key matrix. Everything
is built with gcc's address and undefined-behaviour sanitizers, so a
stray pointer fails the test even when the result happens to look right.

```bash
make test                  # all suites (or: tests/run.sh)
make test SUITES="qspi usb"
tests/run.sh -l            # list the suites
```

Each suite prints one line; the full output is in
`tests/build/logs/<suite>.log`. The exit status is 1 if any suite
failed. Suites whose tools are missing are skipped and say why.

## What you need

- gcc and Python 3: all suites except the four below
- `python`: the MicroPython package (`make mp`, see `docs/BUILD.md`)
- `transfer`: `pip install pyserial`
- `web`: Node.js with Playwright and its Chromium
  (`npm install -g playwright && npx playwright install chromium`)
- `loader`: the arm-none-eabi toolchain and `pip install unicorn`

## The suites

| Suite | Code under test | What it checks |
|-------|-----------------|----------------|
| `flashfs` | `fs/flashfs.c` | The file system on simulated flash: writes, renames, compaction, power cuts at every step, remounts |
| `qspi` | `fs/storage_qspi.c` + `flashfs.c` | The QSPI driver on a model of the controller and the AT25SF641 (SPI/QPI, continuous read, the loader's settings), write protection, a stuck flash |
| `expr` | `apps/common/expr.c` | The expression evaluator |
| `equations` | `apps/equations/` | Quadratics, 2×2 systems, Newton; error messages |
| `keyboard` | `hal/keyboard.c` | Pin set-up, scanning, debouncing, key repeat |
| `tetris` | `apps/tetris/` | Timing and drawing |
| `functions` | `apps/functions/`, `apps/common/analysis.c` | Function entry, graph, trace, zeros, extremes, intersections |
| `editor` | `apps/text_editor/` | 8 KB files, scrolling in both directions |
| `scheduler` | `kernel/scheduler.c` | Task order, sleeping, idle |
| `shell` | `shell/shell.c` | Scrolling output stays on screen |
| `crash` | `hal/fault.c` | The crash report for each kind of fault, on the UART and the screen |
| `sleep` | `kernel/kernel.c` | Sleep mode: keys, USB and clocks while asleep |
| `display` | `hal/display.c` | The ST7789V set-up sequence and what reaches the panel |
| `hal` | `hal/backlight.c`, `led.c`, `battery.c` | Backlight pulses, LED PWM, battery levels and the charge LED |
| `usb` | `usb/usb_device.c`, `usb_cdc.c` | Enumeration like Linux does it, then the transfer protocol, on a simulated OTG core |
| `apps` | calculator, statistics, games, settings | History, statistics, Snake, 2048, saved settings, the language switch |
| `python` | `micropython-port/`, `apps/python_app/` | The MicroPython port: REPL, `input()`, imports, files, modules |
| `transfer` | `usb/usb_cdc.c`, `tools/upload.py`, `transfer.py` | The PC tools against the device side over a pseudo-terminal |
| `web` | `tools/web/uploader.html` | The browser uploader in headless Chromium, with a mocked serial port |
| `loader` | `loader/loader.c` | The internal-flash loader in an emulated Cortex-M7 (Unicorn) with models of the QSPI controller, the AT25SF641 and the keyboard |

## How the code gets onto a PC

- Most suites compile the OS sources as they are, with stubs for what
  they call. Some include a `.c` file (the `*_wrap.c` files) to reach
  its `static` functions.
- `qspi` and `usb` build the drivers with `-DQSPI_SIM` / `-DUSB_SIM`,
  which swaps register access for a simulated controller.
- `crash`, `sleep`, `display` and `hal` use host copies of sources that
  touch registers directly or contain ARM assembly. `gen_host.py` makes
  them in `tests/build/gen/`, replacing each register or instruction
  with a call or variable the test provides. If a source changes so that
  a replacement no longer matches, it stops with an error, so a copy
  can't silently drift from the real code.
- `loader` runs the real ARM binary (`make loader`), plus a build with a
  5 ms busy timeout for the dead-flash case, and the OS image whose
  vector table the loader has to accept.

`common/storage_sim.c` is the storage area as NOR flash in RAM
(programming only clears bits, erase per sector), with power cuts and
write protection on request. Every suite that needs a file system uses
it.
