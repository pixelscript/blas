# =============================================================================
# Blas — side-chain ducker with its own kick and snare, for the Hermetic
# Modular Alchemy Lab (V2).
#
# Built on alchemy-template (standard Daisy workflow, libDaisy core Makefile):
#   ./setup.sh          — fetch the SDK + libDaisy into lib/ and build libDaisy
#   make                — build firmware → build/blas.bin
#   make program-dfu    — flash over USB (module in DFU mode: hold B3 at boot)
#   make program-live   — reboot the running module over USB and flash it
#   make test           — host-side DSP checks (no hardware)
#   make clean          — remove the build tree
# =============================================================================

TARGET = blas

# Blas uses V2-only hardware (CV jacks as trigger inputs and envelope output).
BOARD = v2

ALCHEMY_DIR  = lib/alchemy-sdk
LIBDAISY_DIR = lib/libDaisy

# ── App sources ─────────────────────────────────────────────────────────────
CPP_SOURCES = \
    src/blas.cpp \
    src/blas_dsp.cpp

# ── Alchemy SDK, compiled straight from the submodule ───────────────────────
CPP_SOURCES += $(sort $(shell find $(ALCHEMY_DIR)/framework/src -name '*.cpp'))
CPP_SOURCES += $(sort $(wildcard $(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/src/*.cpp))

C_INCLUDES += \
    -Isrc \
    -I$(ALCHEMY_DIR)/framework/include \
    -I$(ALCHEMY_DIR)/hardware/include \
    -I$(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/include

C_DEFS += -DALCHEMY_BOARD_V2

# ── Version stamp (HostLink HELLO + descriptor) ─────────────────────────────
BLAS_VERSION  := $(shell cat version.txt 2>/dev/null || echo 0.0.0)
BLAS_GIT_HASH := $(shell git describe --always --dirty --abbrev=7 --exclude '*' 2>/dev/null || echo dev)
C_DEFS += -DBLAS_VERSION='"$(BLAS_VERSION)"' -DBLAS_GIT_HASH='"$(BLAS_GIT_HASH)"'

OPT = -O2

# ── Daisy bootloader build (BOOT_SRAM) ──────────────────────────────────────
APP_TYPE = BOOT_SRAM
LDSCRIPT = $(ALCHEMY_DIR)/cmake/linkers/alchemy_stm32h750ib_sram.lds

# The Alchemy SDK requires C++17 (libDaisy's default is gnu++14).
CPP_STANDARD = -std=gnu++17

# ── libDaisy core Makefile does the rest ────────────────────────────────────
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: libdaisy
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

# ── Flash without touching the module ───────────────────────────────────────
USBPID ?= df11

.PHONY: program-live
program-live: all
	node $(ALCHEMY_DIR)/tools/hostlink-cli/hostlink.mjs reboot bootloader
	dfu-util -w -a 0 -s $(FLASH_ADDRESS):leave -D $(BUILD_DIR)/$(TARGET_BIN) -d ,0483:$(USBPID)

# ── Host-side checks (no hardware, native compiler) ─────────────────────────
#   host_test     the audio engine: drums, ducking depth, the three duck types
#   surface_test  the hold gesture and CV depths, against the SDK's host stubs
HOST_CXX ?= g++
SURFACE_SDK_SRCS = \
    $(ALCHEMY_DIR)/framework/src/surface/pager.cpp \
    $(ALCHEMY_DIR)/framework/src/surface/settings.cpp \
    $(ALCHEMY_DIR)/framework/src/surface/preset_gesture_ui.cpp \
    $(ALCHEMY_DIR)/framework/src/surface/presets.cpp \
    $(ALCHEMY_DIR)/framework/src/surface/virtual_knob.cpp \
    $(ALCHEMY_DIR)/framework/src/surface/button_bank.cpp \
    $(ALCHEMY_DIR)/framework/src/host_link/describe.cpp \
    $(ALCHEMY_DIR)/framework/src/host_link/descriptor.cpp \
    $(ALCHEMY_DIR)/framework/src/host_link/json_check.cpp \
    $(sort $(shell find $(ALCHEMY_DIR)/framework/src/led -name '*.cpp')) \
    $(sort $(wildcard $(ALCHEMY_DIR)/hardware/alchemy-lab/v1/src/*.cpp))

.PHONY: test
test:
	@mkdir -p build
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -Isrc tests/host_test.cpp src/blas_dsp.cpp -o build/host_test
	./build/host_test
	$(HOST_CXX) -O1 -std=c++17 -I$(ALCHEMY_DIR)/stubs -Isrc \
	    -I$(ALCHEMY_DIR)/framework/include -I$(ALCHEMY_DIR)/hardware/include \
	    -I$(ALCHEMY_DIR)/hardware/alchemy-lab/v1/include -I$(ALCHEMY_DIR)/hardware/alchemy-lab/v1/src \
	    tests/surface_test.cpp src/blas_dsp.cpp $(SURFACE_SDK_SRCS) -o build/surface_test
	./build/surface_test
