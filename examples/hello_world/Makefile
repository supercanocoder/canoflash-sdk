# ===========================================================================
# CanoFlash Net example. This Makefile is hand-written on purpose: the
# devkitPro template works fine, but it is 180 lines, and what matters here is
# seeing at a glance WHAT you need in order to use the SDK.
#
#   make        builds hello_world.gba
#   make clean  removes what it built
# ===========================================================================

ifeq ($(strip $(DEVKITARM)),)
DEVKITARM := /opt/devkitpro/devkitARM
endif
ifeq ($(strip $(DEVKITPRO)),)
DEVKITPRO := /opt/devkitpro
endif

PREFIX  := $(DEVKITARM)/bin/arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
GBAFIX  := $(DEVKITPRO)/tools/bin/gbafix

TARGET  := hello_world
SDK     := ../..

# The only two things you need to add to a project in order to use the SDK:
# the include directory and the .c file.
INCLUDES := -I$(SDK)/include
SOURCES  := main.c $(SDK)/src/canoflash.c

ARCH    := -mthumb -mthumb-interwork -mcpu=arm7tdmi
CFLAGS  := $(ARCH) $(INCLUDES) -std=c99 -O2 -Wall -Wextra \
           -fomit-frame-pointer -ffast-math
LDFLAGS := $(ARCH) -specs=gba.specs

all: $(TARGET).gba

$(TARGET).elf: $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(TARGET).gba: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	$(GBAFIX) $@ -t$(TARGET)

clean:
	rm -f $(TARGET).elf $(TARGET).gba

.PHONY: all clean
