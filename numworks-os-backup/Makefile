# ================================================================
# NumWorks OS — Build System
# Target: STM32F730V8T6 (NumWorks N0110)
# Toolchain: arm-none-eabi-gcc
# ================================================================

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
AS      := $(CROSS)gcc -x assembler-with-cpp
LD      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size

TARGET  := numworks_os
BUILD   := build

# CPU flags
CPU     := cortex-m7
FPU     := fpv5-d16
FLOAT   := hard
MCU     := -mcpu=$(CPU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT)

# Source files
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
    shell/shell.c \
    shell/commands.c \
    ui/filemanager.c \
    ui/font.c \
    usb/usb_cdc.c \
    micropython-port/mp_port.c

SRCS_S := bootloader/startup_stm32f730.s

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(SRCS_C)) \
        $(patsubst %.s,$(BUILD)/%.o,$(SRCS_S))

# Auto-detect arm-none-eabi newlib include path (needed on macOS with Homebrew)
GCC_VERSION := $(shell $(CC) -dumpversion)
GCC_SYSROOT := $(shell $(CC) -print-sysroot 2>/dev/null)
GCC_LIBDIR  := $(shell $(CC) -print-file-name=include 2>/dev/null)

# On macOS the compiler uses include_next to chain to a system stdint.h that
# doesn't exist for bare-metal targets.  Supplying the newlib headers via
# -isystem breaks the chain and provides the correct freestanding stdint.h.
NEWLIB_INC  := $(GCC_SYSROOT)/include
ifeq ($(wildcard $(NEWLIB_INC)/stdint.h),)
  # Homebrew layout: sysroot is empty; headers live next to the compiler
  CC_PREFIX   := $(shell dirname $(shell which $(CC)))
  NEWLIB_INC  := $(CC_PREFIX)/../arm-none-eabi/include
endif

# Compiler flags
CFLAGS := $(MCU) \
    -std=c11 -Os \
    -ffunction-sections -fdata-sections \
    -fno-exceptions -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fshort-enums \
    -Wall -Wextra -Wno-unused-parameter \
    -DSTM32F730xx -DARM_MATH_CM7 \
    -isystem /usr/local/Cellar/arm-gcc-bin@10/10.3-2021.10_1/arm-none-eabi/include \
    -Iinclude -Ihal -Ikernel -Ifs -Iusb -Ishell -Iui -Ibootloader \
    -Imicropython-port \
    -Imicropython/ports/minimal \
    -Imicropython -Imicropython/py

ifdef DEBUG
    CFLAGS += -DDEBUG -g3
else
    CFLAGS += -DNDEBUG
endif

# Linker flags
MP_LIB  := micropython/ports/minimal/build/libmicropython.a
LDFLAGS := $(MCU) \
    -T linker/numworks.ld \
    -Wl,--gc-sections \
    -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
    -nostartfiles \
    -nodefaultlibs \
    -L/usr/local/Cellar/arm-gcc-bin@10/10.3-2021.10_1/arm-none-eabi/lib/thumb/v7e-m+fp/hard \
    -L/usr/local/Cellar/arm-gcc-bin@10/10.3-2021.10_1/lib/gcc/arm-none-eabi/10.3.1/thumb/v7e-m+fp/hard \
    -lc -lm -lgcc

.PHONY: all clean flash dfu size dump mp

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
	@$(OBJCOPY) -O binary --remove-section=.fs_region $< $@
	@echo "  BIN $(BUILD)/$(TARGET).bin"

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<

# Build MicroPython static library
mp:
	$(MAKE) -C micropython/ports/minimal \
	    CROSS_COMPILE=$(CROSS) \
	    CFLAGS_EXTRA="-DMICROPY_CONFIG_FILE='\"../../micropython-port/mpconfigport.h\"'" \
	    libmicropython.a

# Flash via OpenOCD + ST-Link
flash: $(BUILD)/$(TARGET).bin
	openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
	    -c "init; reset halt" \
	    -c "flash write_image erase $(BUILD)/$(TARGET).bin 0x08000000" \
	    -c "verify_image $(BUILD)/$(TARGET).bin 0x08000000" \
	    -c "reset run; exit"

# Flash via DFU (hold HOME+RESET to enter DFU mode on NumWorks)
dfu: $(BUILD)/$(TARGET).bin
	dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D $(BUILD)/$(TARGET).bin

dump: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d -S $< > $(BUILD)/$(TARGET).s

clean:
	rm -rf $(BUILD)
