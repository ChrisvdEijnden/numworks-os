# ================================================================
# NumWorks OS — Build System (N0120 Custom Firmware)
# Target: STM32F730V8T6 (NumWorks N0120)
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
FPU     := fpv5-d16
FLOAT   := hard
MCU     := -mcpu=$(CPU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT)

# ── Source files ─────────────────────────────────────────────────
SRCS_C := \
    main.c \
    bootloader/boot.c \
    kernel/kernel.c \
    kernel/scheduler.c \
    kernel/memory.c \
    hal/hal.c \
    hal/display.c \
    hal/keyboard.c \
    hal/uart.c \
    hal/timer.c \
    hal/clocks.c \
    fs/flashfs.c \
    fs/ff.c \
    fs/diskio.c \
    shell/shell.c \
    shell/commands.c \
    ui/filemanager.c \
    ui/font.c \
    usb/usb_cdc.c \
    usb/usb_host.c \
    micropython-port/mp_port.c \
    apps/home/home.c \
    apps/calculator/calculator.c \
    apps/functions/functions.c \
    apps/equations/equations.c \
    apps/python_app/python_app.c \
    apps/tetris/tetris.c \
    apps/docs_app/docs_app.c \
    apps/settings/settings.c \
    apps/photo_viewer/photo_viewer.c \
    apps/text_editor/text_editor.c

SRCS_S := bootloader/startup_stm32f730.s

# Newlib headers are provided by include/stdint.h, include/stdbool.h,
# include/stddef.h (bare-metal versions that shadow the broken GCC
# hosted headers on macOS Homebrew arm-none-eabi-gcc).
# -Iinclude is already in CFLAGS so they are found first.
NEWLIB_ISYSTEM :=

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
    -std=c11 -Os \
    -ffunction-sections -fdata-sections \
    -fno-exceptions -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fshort-enums \
    -Wall -Wextra -Wno-unused-parameter \
    -DSTM32F730xx -DARM_MATH_CM7 \
    $(GCC_ISYSTEM) \
    -Iinclude -Ihal -Ikernel -Ifs -Iusb -Ishell -Iui -Ibootloader \
    -Imicropython-port \
    -Imicropython/ports/minimal \
    -Imicropython -Imicropython/py

ifdef DEBUG
    CFLAGS += -DDEBUG -g3
else
    CFLAGS += -DNDEBUG
endif

# ── Linker flags ─────────────────────────────────────────────────
MP_LIB    := micropython/ports/minimal/build/libmicropython.a
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

# Add MicroPython lib if it exists
ifneq ($(wildcard $(MP_LIB)),)
    LDFLAGS += $(MP_LIB)
endif

.PHONY: all clean flash dfu size dump mp phi delta openocd help restore print-libs print-newlib

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

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "  AS  $<"
	@$(AS) $(MCU) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo "  LD  $@"
	@$(LD) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O binary $< $@
	@echo "  BIN $(BUILD)/$(TARGET).bin"

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<

# ── Build MicroPython static library ─────────────────────────────
mp:
	$(MAKE) -C micropython/ports/minimal \
	    CROSS_COMPILE=$(CROSS) \
	    CFLAGS_EXTRA="-DMICROPY_CONFIG_FILE='\"../../micropython-port/mpconfigport.h\"'" \
	    libmicropython.a

# ── Flash methods ─────────────────────────────────────────────────

# Via Phi bootloader (recommended for N0120)
# Connect USB-C, hold RESET, then run:
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
flash: $(BUILD)/$(TARGET).bin
	$(call check_link_addr,0x90000000)
	@if [ "$(CONFIRM)" != "overwrite-stock-firmware" ]; then \
	  echo "This overwrites the stock NumWorks firmware at 0x90000000, and"; \
	  echo "epsilon-qspi-backup.bin can NOT bring it back (see docs/BUILD.md,"; \
	  echo "'Restore Official Firmware')."; \
	  echo "To go ahead anyway: make flash CONFIRM=overwrite-stock-firmware"; \
	  exit 1; \
	fi
	@echo "Flashing to N0120 QSPI via rescue mode..."
	@echo "Calculator must show numworks.com/rescue screen."
	dfu-util -d 0483:a291 -a 0 -s 0x90000000:leave -D $(BUILD)/$(TARGET).bin

dfu:   phi

dump: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d -S $< > $(BUILD)/$(TARGET).s

clean:
	rm -rf $(BUILD)

help:
	@echo "Targets:"
	@echo "  all      - Build firmware"
	@echo "  phi      - Flash via Phi bootloader (N0120)"
	@echo "  delta    - Flash via Delta bootloader"
	@echo "  openocd  - Flash via ST-Link (dev)"
	@echo "  flash    - Overwrite stock firmware in QSPI (needs CONFIRM=...)"
	@echo "  mp       - Build MicroPython static library"
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

# epsilon-qspi-backup.bin was taken after an old build of this OS had
# already been flashed over the start of the stock firmware, so it can't
# restore a bootable calculator. Refuse rather than flash it.
restore:
	@echo "Refusing: epsilon-qspi-backup.bin is not a working stock image."
	@echo "Its first 108 KB (0x0-0x1AFFF) is an old build of this OS, not the"
	@echo "Epsilon kernel, so flashing it would leave the calculator unbootable."
	@echo "Restore the official firmware as described in docs/BUILD.md."
	@exit 1
