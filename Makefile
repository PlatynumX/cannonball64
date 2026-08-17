BUILD_DIR := build
SOURCE_DIR := .
ROM_NAME := cannonball64-r2

# The build workflow (or scripts/fetch_core.sh) populates this.
CORE_DIR := vendor/cannonball
STATIC_LINKING := 0
platform := n64

ifeq ("$(wildcard $(CORE_DIR)/Makefile.common)","")
$(error Cannonball core missing. Run ./scripts/fetch_core.sh first)
endif

# Reuse the Libretro core's authoritative source list and include flags.
include $(CORE_DIR)/Makefile.common

# Cannonboard serial/cabinet passthrough is a desktop feature, not needed on N64.
SOURCES_CXX := $(filter-out \
	$(CORE_DIR)/src/main/cannonboard/interface.cpp \
	$(CORE_DIR)/src/main/cannonboard/asyncserial.cpp, \
	$(SOURCES_CXX))

include $(N64_INST)/include/n64.mk

CFLAGS   += $(INCFLAGS) $(FLAGS)
CXXFLAGS += $(INCFLAGS) $(FLAGS)

# Third-party upstream code should not fail the N64 build because of warnings.
N64_C_AND_CXX_FLAGS += -Wno-error

HOST_SOURCES_CXX := src/n64_frontend.cpp

CORE_C_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES_C))
CORE_CXX_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SOURCES_CXX))
HOST_OBJS     := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(HOST_SOURCES_CXX))

OBJS := $(HOST_OBJS) $(CORE_C_OBJS) $(CORE_CXX_OBJS)

all: $(ROM_NAME).z64

$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS)

$(ROM_NAME).z64: N64_ROM_TITLE="Cannonball 64 r2"
$(ROM_NAME).z64: N64_ROM_CONTROLLER1=joypad
$(ROM_NAME).z64: N64_ROM_REGIONFREE=true

clean:
	rm -rf $(BUILD_DIR) $(ROM_NAME).z64

print-sources:
	@echo "C sources:"
	@printf '%s\n' $(SOURCES_C)
	@echo "C++ sources:"
	@printf '%s\n' $(SOURCES_CXX)

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

.PHONY: all clean print-sources
