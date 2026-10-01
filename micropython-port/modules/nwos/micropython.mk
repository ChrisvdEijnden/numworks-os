# NumWorks OS modules for MicroPython: `display`, `time`, `random`, `os`
# and the builtin open().
# Only scanned for QSTRs / module registration here; the firmware's
# Makefile compiles these files.
NWOS_MOD_DIR := $(USERMOD_DIR)
SRC_USERMOD_C += $(NWOS_MOD_DIR)/moddisplay.c $(NWOS_MOD_DIR)/nwos_open.c \
                 $(NWOS_MOD_DIR)/modtime.c $(NWOS_MOD_DIR)/modrandom.c \
                 $(NWOS_MOD_DIR)/modos.c
