# Thin convenience wrapper around the CMake/Ninja build described in
# doc/BUILDING.md. Not the project's real build system - just fewer keystrokes
# for the common cases. Every target below is safe to run repeatedly; `cmake
# -S -B` reconfigures in place instead of failing on an existing build dir.
#
# Usage:
#   make                          # configure + build the default variant (Pico W + REON)
#   make BOARD=pico2_w ADAPTER=STACKSMASHING
#   make picow-reon                # Pico W      + REON            -> build/picow-reon/
#   make picow-sm                  # Pico W      + STACKSMASHING   -> build/picow-sm/
#   make pico2w-reon                # Pico 2 W    + REON            -> build/pico2w-reon/
#   make pico2w-sm                  # Pico 2 W    + STACKSMASHING   -> build/pico2w-sm/
#   make all-variants               # build all four of the above
#   make clean                      # remove build/ entirely (every variant, board/adapter alike)
#   make help
#
# `clean` always wipes all of build/, not just the variant you most recently
# built: with one build directory per board/adapter combination (see
# BUILD_DIR below), "clean the current one" is ambiguous the moment more than
# one has ever been configured, so this Makefile doesn't try to guess.

BOARD          ?= pico_w
ADAPTER        ?= REON
BUILD_TYPE     ?= Release
IMPLEMENTATION ?=

BUILD_DIR      ?= build/$(BOARD)_$(ADAPTER)

CMAKE_ARGS := -DPICO_BOARD=$(BOARD) -DADAPTER=$(ADAPTER) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
ifneq ($(strip $(IMPLEMENTATION)),)
CMAKE_ARGS += -DPICOADAPTER_IMPLEMENTATION=$(IMPLEMENTATION)
endif
ifneq ($(strip $(VERSION)),)
CMAKE_ARGS += -DPICO_ADAPTER_SOFTWARE=$(VERSION)
endif

.PHONY: all build configure clean \
        picow-reon picow-sm pico2w-reon pico2w-sm all-variants help

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja $(CMAKE_ARGS)

build: configure
	cmake --build $(BUILD_DIR) --parallel

clean:
	rm -rf build

picow-reon:
	$(MAKE) BOARD=pico_w ADAPTER=REON BUILD_DIR=build/picow-reon build

picow-sm:
	$(MAKE) BOARD=pico_w ADAPTER=STACKSMASHING BUILD_DIR=build/picow-sm build

pico2w-reon:
	$(MAKE) BOARD=pico2_w ADAPTER=REON BUILD_DIR=build/pico2w-reon build

pico2w-sm:
	$(MAKE) BOARD=pico2_w ADAPTER=STACKSMASHING BUILD_DIR=build/pico2w-sm build

all-variants: picow-reon picow-sm pico2w-reon pico2w-sm

help:
	@echo "make                         Build Pico W + REON (defaults)"
	@echo "make BOARD=... ADAPTER=...   Build a specific board/pinout combination"
	@echo "make picow-reon              Pico W      + REON"
	@echo "make picow-sm                Pico W      + STACKSMASHING"
	@echo "make pico2w-reon             Pico 2 W    + REON"
	@echo "make pico2w-sm               Pico 2 W    + STACKSMASHING"
	@echo "make all-variants            Build all four of the above"
	@echo "make clean                   Remove build/ entirely (every variant)"
	@echo
	@echo "See doc/BUILDING.md for the full PICO_BOARD/PICOADAPTER_IMPLEMENTATION/ADAPTER reference."
