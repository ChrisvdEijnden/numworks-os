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
    kernel/kernel.c \
    hal/hal.c \
    hal/display.c \
    hal/keyboard.c \
    fs/flashfs.c \
    shell/shell.c \
    ui/filemanager.c \
    usb/usb_cdc.c \
    micropython-port/mp_port.c

SRCS_S := bootloader/startup_stm32f730.s

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(SRCS_C)) \
        $(patsubst %.s,$(BUILD)/%.o,$(SRCS_S))

# Compiler flags
CFLAGS := $(MCU) \
    -std=c11 -Os \
    -ffunction-sections -fdata-sections \
    -fno-exceptions -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fshort-enums \
    -Wall -Wextra -Wno-unused-parameter \
    -DSTM32F730xx -DARM_MATH_CM7 \
    -Iinclude -Ihal -Ikernel -Ifs -Iusb \
    -Imicropython-port \
    -Imicropython/ports/minimal \
    -Imicropython -Imicropython/py

ifdef DEBUG
    CFLAGS += -DDEBUG -g3
else
    CFLAGS += -DNDEBUG
endif

# Linker flags
MP_LIB  := micropython-port/build/libmicropython.a
LDFLAGS := $(MCU) \
    -T linker/numworks.ld \
    -Wl,--gc-sections \
    -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
    -nostartfiles \
    -specs=nano.specs -specs=nosys.specs \
    -lc -lgcc -lm $(MP_LIB)

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
    $(MAKE) -C micropython-port \
        CROSS_COMPILE=$(CROSS) \
        CFLAGS_EXTRA="-DMICROPY_CONFIG_FILE='\"mpconfigport.h\"'" \
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
