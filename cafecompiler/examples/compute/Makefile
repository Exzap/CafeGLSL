DEVKITPRO ?= /opt/devkitpro
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
PORTLIBS_PATH ?= $(DEVKITPRO)/portlibs
WUT_ROOT ?= $(DEVKITPRO)/wut
BUILD ?= build

APP_NAME := CafeGLSL Compute
APP_AUTHOR := CafeGLSL

include $(WUT_ROOT)/share/wut_rules
CFLAGS := $(MACHDEP) -O2 -Wall -Wextra -D__WUT__ -D__WIIU__ -I$(WUT_ROOT)/include
LDFLAGS := $(MACHDEP) $(RPXSPECS) -L$(WUT_ROOT)/lib
LD := $(CC)
LIBS := -lwut
OFILES := $(BUILD)/main.o

.PHONY: all clean
all: $(BUILD)/cafeglsl-compute.wuhb

$(BUILD):
	mkdir -p $@

$(BUILD)/main.o: main.c ../../cafe_gx2.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cafeglsl-compute.elf: $(OFILES)
	$(LD) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

clean:
	rm -rf $(BUILD)
