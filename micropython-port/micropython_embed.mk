# ================================================================
# Generates micropython_embed/ — the MicroPython core plus the headers
# generated for this configuration (mpconfigport.h) and our modules
# (modules/*). Run through `make mp` in the top-level Makefile.
# ================================================================
MICROPYTHON_TOP ?= ../micropython
USER_C_MODULES  = modules

include $(MICROPYTHON_TOP)/ports/embed/embed.mk
