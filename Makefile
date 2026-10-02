# ================================================================
# NumWorks OS — Build System (custom firmware for the NumWorks N0110)
# Target: STM32F730V8T6 (NumWorks N0110; the N0120 has an STM32H7)
# Toolchain: arm-none-eabi-gcc
# ================================================================

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
AS      := $(CROSS)gcc -x assembler-with-cpp
LD      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size

TARGET  := numworks_os_n0120
BUILD   := build

CPU     := cortex-m7
# The STM32F730 has a single-precision FPU: fpv5-d16 would emit (and pull
# in a newlib built with) double-precision instructions that fault on it.
# Doubles still work, through libgcc's software routines.
FPU     := fpv5-sp-d16
FLOAT   := hard
MCU     := -mcpu=$(CPU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT)

# ── Source files ─────────────────────────────────────────────────
SRCS_C := \
    main.c \
    bootloader/boot.c \
    kernel/kernel.c \
    kernel/scheduler.c \
    hal/hal.c \
    hal/display.c \
    hal/keyboard.c \
    hal/uart.c \
    hal/syscalls.c \
    hal/led.c \
    hal/backlight.c \
    hal/battery.c \
    hal/fault.c \
    hal/clocks.c \
    fs/flashfs.c \
    fs/storage_qspi.c \
    shell/shell.c \
    shell/commands.c \
    ui/filemanager.c \
    ui/line_input.c \
    ui/font.c \
    ui/lang.c \
    apps/settings/prefs.c \
    ui/battery_icon.c \
    usb/usb_cdc.c \
    usb/usb_device.c \
    micropython-port/mp_port.c \
    apps/common/expr.c \
    apps/common/analysis.c \
    apps/common/stats.c \
    apps/home/home.c \
    apps/calculator/calculator.c \
    apps/functions/functions.c \
    apps/equations/equations.c \
    apps/python_app/python_app.c \
    apps/tetris/tetris.c \
    apps/docs_app/docs_app.c \
    apps/settings/settings.c \
    apps/text_editor/text_editor.c \
    apps/statistics/statistics.c \
    apps/games/games.c \
    apps/games/snake.c \
    apps/games/g2048.c

SRCS_S := bootloader/startup_stm32f730.s

# libgcc.a location — needed for compiler runtime (divide, float etc.)
_LIBGCC     := $(shell $(CC) $(MCU) -print-libgcc-file-name 2>/dev/null)
_LIBGCC_DIR := $(shell dirname $(_LIBGCC))

# Supply GCC's include-fixed dir for string.h, stdio.h, stdarg.h etc.
# This dir has hosted headers with include_next chains already resolved.
_GCC_INCFIXED := $(wildcard $(_LIBGCC_DIR)/include-fixed)
_GCC_INC      := $(wildcard $(_LIBGCC_DIR)/include)
GCC_ISYSTEM   := $(if $(_GCC_INCFIXED),-isystem $(_GCC_INCFIXED)) $(if $(_GCC_INC),-isystem $(_GCC_INC))

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(SRCS_C)) \
        $(patsubst %.s,$(BUILD)/%.o,$(SRCS_S))

# ── Compiler flags ───────────────────────────────────────────────
CFLAGS := $(MCU) \
    -std=gnu11 -Os \
    -ffunction-sections -fdata-sections \
    -fno-exceptions -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fshort-enums \
    -Wall -Wextra -Wno-unused-parameter \
    -DSTM32F730xx -DARM_MATH_CM7 \
    $(GCC_ISYSTEM) \
    -Iinclude -Ihal -Ikernel -Ifs -Iusb -Ishell -Iui -Ibootloader \
    -Imicropython-port

ifdef DEBUG
    CFLAGS += -DDEBUG -g3
else
    CFLAGS += -DNDEBUG
endif

# ── Linker flags ─────────────────────────────────────────────────
LDSCRIPT  := linker/numworks_n0120.ld

# ── Library resolution ───────────────────────────────────────────
# arm-none-eabi-gcc (Homebrew) ships only the compiler, not newlib.
# libc.a / libm.a come from the separate arm-gcc-bin@10 formula.
# Strategy:
#  1. Ask gcc where it thinks libc.a is (works when newlib is bundled)
#  2. If gcc returns just "libc.a" (not found), search known locations
#  3. Use full absolute paths to avoid ld search path issues

_LIBC_A_GCC  := $(shell $(CC) $(MCU) -print-file-name=libc.a 2>/dev/null)
_LIBM_A_GCC  := $(shell $(CC) $(MCU) -print-file-name=libm.a 2>/dev/null)

# Detect if gcc found a real path (contains /) or just returned the name
_LIBC_FOUND  := $(filter /%,$(_LIBC_A_GCC))
_LIBM_FOUND  := $(filter /%,$(_LIBM_A_GCC))

# Fallback: search arm-gcc-bin@10 which we know has newlib on this machine
# Multilib subdir for cortex-m7 hard-float is thumb/v7e-m+fp/hard/
_MULTILIB    := thumb/v7e-m+fp/hard

_LIBC_SEARCH := $(firstword $(wildcard \
    /usr/local/Cellar/arm-gcc-bin@10/*/arm-none-eabi/lib/$(_MULTILIB)/libc.a \
    /usr/local/Cellar/arm-gcc-bin@10/*/arm-none-eabi/lib/libc.a \
    /opt/homebrew/Cellar/arm-gcc-bin@10/*/arm-none-eabi/lib/$(_MULTILIB)/libc.a \
    /usr/local/arm-none-eabi/lib/$(_MULTILIB)/libc.a \
    /usr/local/arm-none-eabi/lib/libc.a))

_LIBM_SEARCH := $(firstword $(wildcard \
    /usr/local/Cellar/arm-gcc-bin@10/*/arm-none-eabi/lib/$(_MULTILIB)/libm.a \
    /usr/local/Cellar/arm-gcc-bin@10/*/arm-none-eabi/lib/libm.a \
    /opt/homebrew/Cellar/arm-gcc-bin@10/*/arm-none-eabi/lib/$(_MULTILIB)/libm.a \
    /usr/local/arm-none-eabi/lib/$(_MULTILIB)/libm.a \
    /usr/local/arm-none-eabi/lib/libm.a))

_LIBC_A := $(if $(_LIBC_FOUND),$(_LIBC_A_GCC),$(_LIBC_SEARCH))
_LIBM_A := $(if $(_LIBM_FOUND),$(_LIBM_A_GCC),$(_LIBM_SEARCH))

ifeq ($(_LIBC_A),)
  $(warning *** Could not find libc.a — run 'make print-libs' for diagnostics)
  $(warning *** You may need: brew install arm-gcc-bin@10)
endif

LDFLAGS := $(MCU) \
    -T $(LDSCRIPT) \
    -Wl,--gc-sections \
    -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
    -nostartfiles \
    -nodefaultlibs \
    $(_LIBC_A) $(_LIBM_A) $(_LIBGCC)
# The QSPI flash routines run from RAM (in .data), so that segment is
# writable and executable on purpose. Linkers from binutils 2.39 on warn
# about it; tell them it's intended, if they know the option.
LDFLAGS += $(shell $(CC) -Wl,--no-warn-rwx-segments -Wl,--version > /dev/null 2>&1 \
                   && echo -Wl,--no-warn-rwx-segments)

# The C library's headers (newlib). Where the compiler doesn't find them
# by itself (Homebrew arm-none-eabi-gcc ships without newlib), use the
# ones next to the libc.a found above.
_NEWLIB_INC := $(firstword $(wildcard \
    $(dir $(_LIBC_A))../../../../include/string.h \
    $(dir $(_LIBC_A))../include/string.h))
NEWLIB_ISYSTEM := $(if $(_NEWLIB_INC),-isystem $(dir $(_NEWLIB_INC)))
CFLAGS += $(NEWLIB_ISYSTEM)

# ── MicroPython (optional) ───────────────────────────────────────
# `make mp` generates micropython-port/micropython_embed/ (MicroPython's
# embed port: core sources + headers generated for our configuration)
# from a MicroPython checkout. When it exists, Python is compiled in;
# otherwise mp_port.c is a stub that says Python isn't available.
MICROPYTHON ?= micropython
MP_EMBED    := micropython-port/micropython_embed

ifneq ($(wildcard $(MP_EMBED)/genhdr/qstrdefs.generated.h),)
MP_CORE_SRCS := $(wildcard $(MP_EMBED)/py/*.c) \
                $(MP_EMBED)/shared/runtime/gchelper_generic.c \
                $(MP_EMBED)/port/embed_util.c
MP_GLUE_SRCS := micropython-port/modules/nwos/moddisplay.c \
                micropython-port/modules/nwos/nwos_open.c \
                micropython-port/modules/nwos/modtime.c \
                micropython-port/modules/nwos/modrandom.c \
                micropython-port/modules/nwos/modos.c \
                micropython-port/modules/nwos/modkandinsky.c \
                micropython-port/modules/nwos/modion.c
SRCS_C += $(MP_CORE_SRCS) $(MP_GLUE_SRCS)
OBJS   += $(patsubst %.c,$(BUILD)/%.o,$(MP_CORE_SRCS) $(MP_GLUE_SRCS))

MP_CFLAGS := $(MCU) -std=gnu99 -Os -ffunction-sections -fdata-sections \
    -fshort-enums -DNDEBUG -DNWOS_MICROPYTHON \
    $(GCC_ISYSTEM) $(NEWLIB_ISYSTEM) \
    -Imicropython-port -I$(MP_EMBED)
# Third-party core: no warnings. Our glue: the usual ones.
$(patsubst %.c,$(BUILD)/%.o,$(MP_CORE_SRCS)): CFLAGS := $(MP_CFLAGS) -w
$(patsubst %.c,$(BUILD)/%.o,$(MP_GLUE_SRCS) micropython-port/mp_port.c): \
    CFLAGS := $(MP_CFLAGS) -Wall -Wextra -Wno-unused-parameter
# The embed port's nlr_jump_fail() hangs; ours shows the crash screen
MP_LDFLAGS := -Wl,--wrap=nlr_jump_fail
endif

.PHONY: all clean distclean flash dfu size dump mp phi delta openocd help print-libs print-newlib \
        loader flash-loader backup-internal restore-internal test sim run-sim

# Refuse to flash the image at an address it isn't linked for: it would
# not run there (the vector table and every absolute address would be wrong).
define check_link_addr
	@vma=$$($(OBJDUMP) -h $(BUILD)/$(TARGET).elf | awk '$$2 == ".isr_vector" { print "0x" $$4 }'); \
	if [ -z "$$vma" ] || [ "$$(($$vma))" -ne "$$(($(1)))" ]; then \
	  echo "error: $(TARGET) is linked for $${vma:-an unknown address}, not $(1);"; \
	  echo "       it would not run there. Change LDSCRIPT before flashing to $(1)."; \
	  exit 1; \
	fi
endef

all: $(BUILD)/$(TARGET).bin size

# mp_port.c builds with or without MicroPython: after `make mp`, build
# it again (the flags changed, which make can't see by itself). Below
# `all`, so it isn't the default target.
ifneq ($(MP_CORE_SRCS),)
$(BUILD)/micropython-port/mp_port.o: $(MP_EMBED)/genhdr/qstrdefs.generated.h
endif

# -MMD -MP: also write a .d file listing the headers each object uses,
# so changing a header (e.g. include/config.h) rebuilds what depends on it
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "  AS  $<"
	@$(AS) $(MCU) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo "  LD  $@"
	@$(LD) $(OBJS) $(LDFLAGS) $(MP_LDFLAGS) -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O binary $< $@
	@echo "  BIN $(BUILD)/$(TARGET).bin"

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<

# ── Generate the MicroPython embed package ───────────────────────
# Needs a MicroPython checkout (default ./micropython) and python3:
#   git clone https://github.com/micropython/micropython
#   make mp && make
mp:
	@test -f $(MICROPYTHON)/ports/embed/embed.mk || { \
	  echo "error: no MicroPython checkout at '$(MICROPYTHON)'."; \
	  echo "       git clone https://github.com/micropython/micropython"; \
	  echo "       (or: make mp MICROPYTHON=/path/to/micropython)"; exit 1; }
	$(MAKE) -C micropython-port -f micropython_embed.mk \
	    MICROPYTHON_TOP=$(abspath $(MICROPYTHON))

# ── Flash methods ─────────────────────────────────────────────────

# Via the Phi bootloader (written for the N0110); needs a linker script
# for 0x08040000 (see docs/BUILD.md)
phi: $(BUILD)/$(TARGET).bin
	$(call check_link_addr,0x08040000)
	@echo "Flashing via Phi bootloader at 0x08040000 ..."
	dfu-util -d 0483:df11 -a 0 -s 0x08040000:leave -D $(BUILD)/$(TARGET).bin

# Via Delta bootloader
delta: $(BUILD)/$(TARGET).bin
	$(call check_link_addr,0x08010000)
	@echo "Flashing via Delta bootloader at 0x08010000 ..."
	dfu-util -d 0483:df11 -a 0 -s 0x08010000:leave -D $(BUILD)/$(TARGET).bin

# Via OpenOCD + ST-Link (development — bypasses signature check)
openocd: $(BUILD)/$(TARGET).bin
	$(call check_link_addr,0x08040000)
	openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
	    -c "init; reset halt" \
	    -c "flash write_image erase $(BUILD)/$(TARGET).bin 0x08040000" \
	    -c "verify_image $(BUILD)/$(TARGET).bin 0x08040000" \
	    -c "reset run; exit"

# Writes over the stock firmware at the start of the external flash.
# The image does not start by itself: the code in the internal flash
# has to jump to its vector table (docs/BUILD.md, "Boot chain").
flash: $(BUILD)/$(TARGET).bin
	$(call check_link_addr,0x90000000)
	@if [ "$(CONFIRM)" != "overwrite-stock-firmware" ]; then \
	  echo "This overwrites the stock NumWorks firmware at 0x90000000. Only"; \
	  echo "NumWorks' own recovery can bring it back (see docs/BUILD.md,"; \
	  echo "'Restore Official Firmware')."; \
	  echo "It boots only once this OS's loader is in the internal flash"; \
	  echo "(make flash-loader; docs/BUILD.md, 'Installing')."; \
	  echo "To go ahead anyway: make flash CONFIRM=overwrite-stock-firmware"; \
	  exit 1; \
	fi
	@echo "Flashing to the N0110's QSPI flash via rescue mode..."
	@echo "Needs NumWorks' DFU (0483:a291): Epsilon's own, or NumWorks' RAM flasher."
	dfu-util -d 0483:a291 -a 0 -s 0x90000000:leave -D $(BUILD)/$(TARGET).bin

dfu:   phi

# ── Internal-flash loader (loader/) ───────────────────────────────
# Starts the OS from the QSPI flash; see "Boot chain" in docs/BUILD.md.
LOADER_CFLAGS := -mcpu=$(CPU) -mthumb -mfloat-abi=soft -Os -std=gnu11 \
    -ffreestanding -fno-tree-loop-distribute-patterns -ffunction-sections \
    -Wall -Wextra -Werror
LOADER_DFU ?= 0483:df11
# Extra defines, e.g. -DLOADER_BUSY_TIMEOUT_MS=5 (tests/run.sh loader)
LOADER_DEFS ?=

loader: $(BUILD)/loader.bin

$(BUILD)/loader.elf: loader/loader.c loader/loader_qspi.h loader/loader.ld
	@mkdir -p $(BUILD)
	$(CC) $(LOADER_CFLAGS) $(LOADER_DEFS) -nostdlib -T loader/loader.ld -Wl,--gc-sections \
	    -Wl,-Map=$(BUILD)/loader.map -o $@ loader/loader.c

$(BUILD)/loader.bin: $(BUILD)/loader.elf
	$(OBJCOPY) -O binary $< $@
	@echo "loader: $$(wc -c < $@) bytes (internal flash, 0x08000000)"

# Save what is in the internal flash now (NumWorks' code) before
# replacing it. Keep the file to yourself: it isn't ours to share.
backup-internal:
	@echo "Calculator in ST's bootloader: hold 6, press RESET, release 6."
	dfu-util -d $(LOADER_DFU) -a 0 -s 0x08000000:65536 -U backup-internal.bin
	@echo "Saved backup-internal.bin"

# Put the saved contents back (then NumWorks' recovery works again)
restore-internal:
	@test -f backup-internal.bin || { echo "No backup-internal.bin here"; exit 1; }
	@echo "Calculator in ST's bootloader: hold 6, press RESET, release 6."
	dfu-util -d $(LOADER_DFU) -a 0 -s 0x08000000:leave -D backup-internal.bin

flash-loader: $(BUILD)/loader.bin
	@if [ "$(CONFIRM)" != "replace-internal-flash" ]; then \
	  echo "This replaces the code in the calculator's internal flash"; \
	  echo "(NumWorks' start-up code) with this OS's loader. Read"; \
	  echo "'Boot chain' and 'Installing' in docs/BUILD.md first,"; \
	  echo "and save the old contents with: make backup-internal"; \
	  echo "To go ahead: make flash-loader CONFIRM=replace-internal-flash"; \
	  exit 1; \
	fi
	@echo "Calculator in ST's bootloader: hold 6, press RESET, release 6."
	dfu-util -d $(LOADER_DFU) -a 0 -s 0x08000000:leave -D $(BUILD)/loader.bin

# Host tests (tests/run.sh); one suite: make test SUITES=qspi
test:
	@tests/run.sh $(SUITES)

# ── Simulator (sim/): the OS in a window on a PC ──────────────────
# The OS's own code, compiled for the PC, on simulated hardware: a
# window (SDL2) for the screen and keys, a file for the flash. Only the
# lowest layer is replaced (sim/); the display, keyboard and crash
# screen drivers are the real ones (copies made by tests/gen_host.py).
# Static pattern rules: GNU make 3.81 (macOS) would otherwise also
# apply the firmware's $(BUILD)/%.o rule to these objects.
SIM_CC     ?= cc
SIM_BUILD  := $(BUILD)/sim
SIM_SDL_CFLAGS = $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SIM_SDL_LIBS   = $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

SIM_OS_SRCS := $(filter-out bootloader/% hal/% kernel/kernel.c fs/storage_qspi.c usb/usb_device.c \
                 $(MP_CORE_SRCS) $(MP_GLUE_SRCS) micropython-port/mp_port.c,$(SRCS_C))
SIM_SRCS    := sim/sim_main.c sim/sim_window.c sim/sim_hw.c sim/sim_display.c \
               sim/sim_keyboard.c sim/sim_fault.c
SIM_GEN     := $(SIM_BUILD)/gen/kernel_host.c $(SIM_BUILD)/gen/display_host.c $(SIM_BUILD)/gen/fault_host.c

# The firmware's warnings; gcc's truncation and uninitialised-variable
# guesses differ on a 64-bit PC (the truncations are on purpose)
SIM_GCC     := $(shell $(SIM_CC) --version 2>/dev/null | grep -qi clang || echo yes)
SIM_WARN     = -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
               $(if $(SIM_GCC),-Wno-format-truncation -Wno-stringop-truncation -Wno-maybe-uninitialized)
SIM_CFLAGS   = -std=gnu11 -O2 -g $(SIM_WARN) -DNWOS_SIM -I. -Itests/display -I$(SIM_BUILD)/gen
SIM_MP_CFLAGS := -std=gnu99 -O2 -g -DNDEBUG -DNWOS_MICROPYTHON -Imicropython-port -I$(MP_EMBED)

SIM_OBJ := $(SIM_BUILD)/obj
SIM_OS_OBJS   := $(patsubst %.c,$(SIM_OBJ)/%.o,$(SIM_OS_SRCS) $(SIM_SRCS))
SIM_GEN_OBJS  := $(patsubst $(SIM_BUILD)/gen/%.c,$(SIM_OBJ)/gen/%.o,$(filter-out %/fault_host.c,$(SIM_GEN)))
SIM_MP_OBJS   := $(patsubst %.c,$(SIM_OBJ)/%.o,$(MP_CORE_SRCS))
SIM_GLUE_OBJS := $(patsubst %.c,$(SIM_OBJ)/%.o,$(MP_GLUE_SRCS) micropython-port/mp_port.c)
SIM_OBJS := $(SIM_OS_OBJS) $(SIM_GEN_OBJS) $(SIM_MP_OBJS) $(SIM_GLUE_OBJS)

sim: $(SIM_BUILD)/numworks-sim

# Start it; arguments with SIM_ARGS, e.g. make run-sim SIM_ARGS=--fresh
run-sim: sim
	$(SIM_BUILD)/numworks-sim $(SIM_ARGS)

$(SIM_BUILD)/numworks-sim: $(SIM_OBJS)
	@if [ -z "$(strip $(SIM_SDL_LIBS))" ]; then \
	  echo "The simulator needs SDL2: brew install sdl2 (macOS) or apt install libsdl2-dev"; exit 1; fi
	@echo "  LD  $@"
	@$(SIM_CC) -o $@ $(SIM_OBJS) $(SIM_SDL_LIBS) -lpthread -lm
	@echo "Simulator: $@ (start it with make run-sim)"

# One run of the generator makes all the copies
$(SIM_BUILD)/gen/stamp: tests/gen_host.py hal/fault.c hal/display.c kernel/kernel.c hal/backlight.c hal/led.c hal/battery.c
	@mkdir -p $(SIM_BUILD)/gen
	@python3 tests/gen_host.py . $(SIM_BUILD)/gen
	@touch $@
$(SIM_GEN): $(SIM_BUILD)/gen/stamp ;

$(SIM_OS_OBJS): $(SIM_OBJ)/%.o: %.c | $(SIM_BUILD)/gen/stamp
	@mkdir -p $(dir $@)
	@echo "  CC  $< (sim)"
	@$(SIM_CC) $(SIM_CFLAGS) $(SIM_EXTRA_$(notdir $*)) -MMD -MP -c $< -o $@

$(SIM_GEN_OBJS): $(SIM_OBJ)/gen/%.o: $(SIM_BUILD)/gen/%.c
	@mkdir -p $(dir $@)
	@echo "  CC  $< (sim)"
	@$(SIM_CC) $(SIM_CFLAGS) -include sim/sim_wfi.h -MMD -MP -c $< -o $@

# MicroPython's core: quiet, as in the firmware. In embed_util.c the
# embed port's nlr_jump_fail() is renamed: sim/sim_hw.c has the one
# that's used (the firmware gets it with --wrap)
$(SIM_MP_OBJS): $(SIM_OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $< (sim)"
	@$(SIM_CC) $(SIM_MP_CFLAGS) -w $(if $(filter %/embed_util,$*),-Dnlr_jump_fail=embed_nlr_jump_fail) \
	    -MMD -MP -c $< -o $@

$(SIM_GLUE_OBJS): $(SIM_OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $< (sim)"
	@$(SIM_CC) $(if $(MP_CORE_SRCS),$(SIM_MP_CFLAGS),$(SIM_CFLAGS)) $(SIM_WARN) -MMD -MP -c $< -o $@

# Per-file extras: the OS's main() runs in a thread; the window needs SDL
SIM_EXTRA_main     = -Dmain=nwos_main
SIM_EXTRA_sim_main = $(SIM_SDL_CFLAGS)
SIM_EXTRA_sim_hw   = $(if $(MP_CORE_SRCS),-DNWOS_SIM_MICROPYTHON)

ifneq ($(MP_CORE_SRCS),)
$(SIM_GLUE_OBJS): $(MP_EMBED)/genhdr/qstrdefs.generated.h   # as for the firmware
endif

-include $(SIM_OBJS:.o=.d)

dump: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d -S $< > $(BUILD)/$(TARGET).s

clean:
	rm -rf $(BUILD)

# Also drop the generated MicroPython package (rebuild with `make mp`)
distclean: clean
	rm -rf $(MP_EMBED) micropython-port/build-embed

help:
	@echo "Targets:"
	@echo "  all      - Build firmware"
	@echo "  phi      - Flash via Phi bootloader (N0110)"
	@echo "  delta    - Flash via Delta bootloader"
	@echo "  openocd  - Flash via ST-Link (dev)"
	@echo "  flash    - Overwrite stock firmware in QSPI (needs CONFIRM=...)"
	@echo "  loader   - Build the internal-flash loader"
	@echo "  flash-loader - Write the loader to internal flash (needs CONFIRM=...)"
	@echo "  backup-internal - Save the internal flash first"
	@echo "  restore-internal - Write that backup back"
	@echo "  test     - Run the host tests (tests/)"
	@echo "  sim      - Build the simulator (the OS in a window; needs SDL2)"
	@echo "  run-sim  - Build and start it"
	@echo "  mp       - Generate MicroPython (needs ./micropython checkout)"
	@echo "  size     - Show firmware size"
	@echo "  clean    - Clean build artefacts"

# ── Debug: print detected paths (run this if build fails) ─────────
print-libs:
	@echo "libc.a (gcc):    $(_LIBC_A_GCC)"
	@echo "libm.a (gcc):    $(_LIBM_A_GCC)"
	@echo "libc.a (search): $(_LIBC_SEARCH)"
	@echo "libm.a (search): $(_LIBM_SEARCH)"
	@echo "libc.a (used):   $(_LIBC_A)"
	@echo "libm.a (used):   $(_LIBM_A)"
	@echo "libgcc.a (used): $(_LIBGCC)"
	@echo ""
	@echo "If libc.a is empty: brew install arm-gcc-bin@10"

print-newlib:
	@echo "Compiler:         $$(which $(CC))"
	@echo "GCC sysroot:      $(GCC_SYSROOT)"
	@echo "_LIBGCC:          $(_LIBGCC)"
	@echo "_LIBGCC_DIR:      $(_LIBGCC_DIR)"
	@echo "NEWLIB_INC:       $(NEWLIB_INC)"
	@echo "stdint.h found:   $(wildcard $(NEWLIB_INC)/stdint.h)"
	@echo "NEWLIB_ISYSTEM:   $(NEWLIB_ISYSTEM)"
	@echo ""
	@echo "If NEWLIB_INC is wrong, override manually:"
	@echo "  make NEWLIB_INC=/correct/arm-none-eabi/include"
	@echo ""
	@echo "Searching for stdint.h ..."
	@find /usr/local /opt/homebrew -name stdint.h 2>/dev/null | grep -i "arm-none-eabi\|newlib" | head -8 || echo "  (none found)"

-include $(OBJS:.o=.d)
