# NumWorks OS — Architecture Document

## Hardware Target

| Item | Detail |
|------|--------|
| Calculator | NumWorks N0110 |
| MCU | STM32F730R8T6 (ARM Cortex-M7 @ 216 MHz) |
| Flash | 512 KB internal |
| RAM | 256 KB SRAM + 64 KB DTCM |
| Display | 320×240 ILI9341 (16-bit parallel FSMC) |
| Input | 9×6 GPIO keypad matrix |
| USB | OTG_FS (PA11/PA12), CDC-ACM virtual serial |

---

## Memory Map

```
Flash (512 KB @ 0x0800_0000)
├── 0x0800_0000 – 0x0800_FFFF  [ 64 KB]  Bootloader + vector table
├── 0x0801_0000 – 0x0806_FFFF  [384 KB]  Kernel + all code + read-only data
└── 0x0807_0000 – 0x0807_FFFF  [ 64 KB]  Flash Filesystem (flashfs)

RAM (256 KB @ 0x2000_0000)
├── 0x2000_0000 – 0x2003_3FFF  [208 KB]  Framebuffer (153 KB) + kernel BSS
├── 0x2003_4000 – 0x2003_DFFF  [ 40 KB]  MicroPython heap (48 KB carved here)
├── 0x2003_E000 – 0x2003_FFFF  [  8 KB]  Kernel allocator pool
└── DTCM 64 KB @ 0x2004_0000           Main stack (grows down from 0x2004_FFFF)
```

### Module Flash Budget

| Module | Target (KB) | Notes |
|--------|-------------|-------|
| Bootloader + startup | 4 | PLL init, vectors |
| Kernel + scheduler | 8 | Event loop, 4 tasks |
| Memory allocator | 1 | Bitmap pool |
| HAL (display + kbd + uart) | 8 | Drivers |
| Font (6×8 bitmap) | 1 | 95 chars × 8 bytes |
| Flash filesystem | 6 | Flat, no malloc |
| Shell + commands | 10 | 11 commands |
| File manager UI | 5 | Navigator |
| USB CDC | 8 | Protocol handler |
| MicroPython core | ~90 | Stripped build |
| **Total** | **~141 KB** | Leaves ~243 KB spare |

### RAM Budget

| Area | Size |
|------|------|
| Framebuffer (320×240×2) | 153,600 B |
| MicroPython heap | 48,000 B |
| Kernel pool | 8,192 B |
| Stack (DTCM) | 65,536 B |
| BSS (globals, buffers) | ~5,000 B |
| **Total** | ~280 KB (fits in 256+64=320 KB) |

---

## System Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        APPLICATIONS                          │
│          Shell/Terminal    │    File Manager UI              │
│          MicroPython REPL  │    USB File Transfer            │
├──────────────────────────────────────────────────────────────┤
│                       KERNEL                                 │
│  Cooperative Event Loop  │  Task Scheduler  │  Event Queue  │
│  Memory Allocator        │  App State FSM                   │
├──────────────────────────────────────────────────────────────┤
│                    FILESYSTEM LAYER                          │
│  flashfs (internal flat FS)  │  FatFs shim (FAT32 overlay)  │
├──────────────────────────────────────────────────────────────┤
│               HARDWARE ABSTRACTION LAYER (HAL)               │
│  Display   │  Keyboard  │  UART   │  Timer  │  USB CDC      │
├──────────────────────────────────────────────────────────────┤
│                    BOOTLOADER                                │
│  PLL @ 216 MHz  │  .data copy  │  .bss zero  │  FPU enable  │
├──────────────────────────────────────────────────────────────┤
│              STM32F730 HARDWARE                              │
└──────────────────────────────────────────────────────────────┘
```

---

## Kernel Design: Cooperative Event Loop

The kernel uses a **cooperative scheduler** — no preemption, no context switching overhead. Each task runs to completion and calls `scheduler_yield()` to hand control back.

```
kernel_main()
    │
    ├─ mem_init()
    ├─ hal_init() → display, keyboard, UART
    ├─ usb_cdc_init()
    ├─ flashfs_init()
    ├─ display_splash()
    │
    └─ while(1):
         scheduler_run_next()
              │
              ├─ task_idle()    [PRIO 0] — idle counter
              ├─ task_input()   [PRIO 2] — keyboard scan → event queue
              ├─ task_display() [PRIO 2] — flush framebuffer to LCD
              └─ task_shell()   [PRIO 2] — process events, USB, commands
```

**Why cooperative?**
- Zero RAM for saved register banks (no preemptive context switch)
- No race conditions between tasks (no shared-state bugs)
- Deterministic latency for keypad response
- Suitable for a calculator workload (no real-time constraints)

---

## Flash Filesystem (flashfs)

**Layout in 64 KB sector 7:**
```
[0x000 – 0x0FF]  Superblock (256 B)
[0x100 – 0xFFF]  Directory: 32 × 40-byte entries = 1,280 B
[0x1000 – end]   File data pool (~60 KB)
```

**Key design choices:**
- Zero malloc — directory lives in a static array shadowed in RAM
- Files are write-once until the sector is erased and rewritten
- Max 32 files, max 8 KB per file
- Flat structure — directory prefix `dir/` simulates folders

---

## Shell Commands

| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `ls` | List files with sizes |
| `cat <file>` | Print file contents |
| `touch <file>` | Create empty file |
| `rm <file>` | Delete file |
| `mkdir <name>` | Create directory entry (prefix alias) |
| `echo <text>` | Print text to screen |
| `run <file.py>` | Execute Python script via MicroPython |
| `mem` | Show RAM + flash usage |
| `fm` | Open graphical file manager |
| `reboot` | Software reset via SCB AIRCR |

---

## USB File Transfer Protocol

Simple line-based text protocol over CDC-ACM (virtual serial port):

```
PC → Calc:   LIST\r\n
Calc → PC:   hello.py 1234\r\nnotes.txt 456\r\nOK\r\n

PC → Calc:   RECV hello.py\r\n
Calc → PC:   DATA 1234\r\n<1234 bytes of data>

PC → Calc:   SEND hello.py 1234\r\n<1234 bytes of data>
Calc → PC:   OK\r\n

PC → Calc:   DEL hello.py\r\n
Calc → PC:   OK\r\n
```

PC-side tool: `tools/upload.py` (Python 3 + pyserial)

---

## MicroPython Integration

MicroPython is linked as a static library built from the `ports/numworks/` port.

**Custom modules available in Python:**

```python
import display
display.fill(display.BLACK)
display.str(10, 50, "Hello NumWorks!", display.WHITE)
display.flush()
```

**Filesystem access from Python:**
```python
# Read a file
data = open("notes.txt").read()

# Files written via shell or USB are immediately available
```

**Build configuration** (`micropython-port/mpconfigport.h`):
- Disabled: `float` (use fixed-point), `uio`, `ussl`, `ujson`
- Enabled: `uos`, `usys`, `umath`, `ustruct`
- Heap: 48 KB
