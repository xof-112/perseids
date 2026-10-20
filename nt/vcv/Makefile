# Perseids for VCV Rack 2 — the disting NT algorithm in a native module.
#   make RACK_DIR=<Rack-SDK>            plugin.dll / .so / .dylib
#   make RACK_DIR=<Rack-SDK> install    into the Rack user folder
#   make -C test                        native host test (no Rack needed)
#   python3 panel_def.py                panel SVG + src/panel_layout.hpp
RACK_DIR ?= ../../../Rack-SDK

NT   := ..
ROOT := ../..
include sources.mk

FLAGS    += $(PERSEIDS_DEFS) $(PERSEIDS_INCS) $(PERSEIDS_MATH)
CFLAGS   += -Wno-unused
# The engines and the NT layer are C++17 (Rack's default is C++11, the last -std wins).
EXTRA_CXXFLAGS += -std=c++17
CXXFLAGS += -Wno-suggest-override -Wno-unused-parameter -Wno-missing-field-initializers

SOURCES += src/plugin.cpp src/Perseids.cpp

# Shared sources from outside this folder (NT layer, firmware engines, CMSIS):
# objects in build/ext, never next to the sources.
EXT_SRC := $(filter-out src/%,$(PERSEIDS_CXX) $(PERSEIDS_C))
OBJECTS += $(patsubst %,build/ext/%.o,$(notdir $(EXT_SRC)))
vpath %.cpp $(sort $(dir $(filter %.cpp,$(EXT_SRC))))
vpath %.c   $(sort $(dir $(filter %.c,$(EXT_SRC))))

DISTRIBUTABLES += res README.md

include $(RACK_DIR)/plugin.mk

build/ext/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/ext/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(wildcard build/ext/*.d)
