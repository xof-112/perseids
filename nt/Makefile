# Perseids-core for the disting NT
#
#   make         plugins/perseids.o  → microSD: /programs/plug-ins/
#   make win     emu/perseids.dll    → %LOCALAPPDATA%\Rack2\disting-nt-plugins\ (nt_emu, Windows)
#   make wintest smoke test of the DLL under Wine
#   make test    native simulation with audio checks (needs nt_emu fonts, see NT_EMU_PATH)
#   make render  display screenshots (render/perseids-screens.png, needs Python + Pillow)
#
# The engines are the firmware's own sources in ../src, built with 16-bit Trails.

NT_API_PATH ?= distingNT_API
NT_EMU_PATH ?= ../../disting-nt-plugins/emu/nt_emu
ROOT        := ..
CMSIS       := $(ROOT)/lib/libDaisy/Drivers/CMSIS-DSP
CMSIS_CORE  := $(ROOT)/lib/libDaisy/Drivers/CMSIS_5/CMSIS/Core/Include
DAISYSP     := $(ROOT)/lib/DaisySP/Source

ENGINE_SRC  := $(ROOT)/src/capture_engine.cpp $(ROOT)/src/spectra_engine.cpp \
               $(ROOT)/src/swarm_engine.cpp $(ROOT)/src/reso_engine.cpp \
               $(DAISYSP)/Effects/wavefolder.cpp $(DAISYSP)/Filters/svf.cpp
PLUGIN_SRC  := perseids_nt.cpp
# CMSIS-DSP subset for the 2048-point RFFT (same list as link_cmsis_dsp.py).
CMSIS_SRC   := $(addprefix $(CMSIS)/Source/, \
               TransformFunctions/arm_rfft_fast_f32.c TransformFunctions/arm_rfft_fast_init_f32.c \
               TransformFunctions/arm_cfft_f32.c TransformFunctions/arm_cfft_init_f32.c \
               TransformFunctions/arm_cfft_radix8_f32.c TransformFunctions/arm_bitreversal2.c \
               BasicMathFunctions/arm_mult_f32.c ComplexMathFunctions/arm_cmplx_mag_f32.c \
               CommonTables/arm_common_tables.c CommonTables/arm_const_structs.c)

DEFS        := -DPERSEIDS_TRAIL_INT16 -DARM_DSP_CONFIG_TABLES -DARM_FFT_ALLOW_TABLES \
               -DARM_TABLE_TWIDDLECOEF_F32_1024 -DARM_TABLE_BITREVIDX_FLT_1024 \
               -DARM_TABLE_TWIDDLECOEF_RFFT_F32_2048
INCS        := -I$(NT_API_PATH)/include -I$(ROOT)/include -I$(DAISYSP) -I$(DAISYSP)/Utility \
               -I$(CMSIS)/Include -I$(CMSIS)/PrivateInclude
# Same float behaviour as the firmware (platformio.ini): no errno, fused multiply-add.
MATH        := -fno-math-errno -ffp-contract=fast

# --- disting NT (Cortex-M7) -------------------------------------------------
ARM_CXX     := arm-none-eabi-c++
ARM_CC      := arm-none-eabi-gcc
ARM_FLAGS   := -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fPIC -O2 $(MATH) \
               -fdata-sections -ffunction-sections -Wall $(DEFS) -DARM_MATH_CM7 $(INCS) -I$(CMSIS_CORE)
ARM_CXXFLAGS := -std=c++17 -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit

ARM_OBJ     := $(patsubst %,build/arm/%.o,$(notdir $(ENGINE_SRC) $(PLUGIN_SRC) $(CMSIS_SRC)))
vpath %.cpp $(ROOT)/src $(DAISYSP)/Effects $(DAISYSP)/Filters .
vpath %.c   $(sort $(dir $(CMSIS_SRC)))

all: plugins/perseids.o

build/arm/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(ARM_CXX) $(ARM_FLAGS) $(ARM_CXXFLAGS) -c -o $@ $<

build/arm/%.c.o: %.c
	@mkdir -p $(@D)
	$(ARM_CC) $(ARM_FLAGS) -std=gnu11 -Wno-unused -c -o $@ $<

# One relocatable object, as the NT expects.
plugins/perseids.o: $(ARM_OBJ)
	@mkdir -p $(@D)
	arm-none-eabi-ld -r -o $@ $^
	@echo "Undefined symbols (resolved by the NT firmware):"
	@arm-none-eabi-nm -u $@ | awk '{print "  " $$2}'

# --- nt_emu on Windows (VCV Rack) ------------------------------------------
WINCXX      ?= x86_64-w64-mingw32-g++
WINCC       ?= x86_64-w64-mingw32-gcc
WIN_FLAGS   := -O2 $(MATH) -D__GNUC_PYTHON__ $(DEFS) $(INCS) -include emu/nt_win_shim.h
WIN_OBJ     := $(patsubst %,build/win/%.o,$(notdir $(ENGINE_SRC) $(PLUGIN_SRC) $(CMSIS_SRC))) \
               build/win/nt_win_shim.cpp.o

build/win/%.cpp.o: %.cpp emu/nt_win_shim.h
	@mkdir -p $(@D)
	$(WINCXX) -std=c++17 -Wall -Wno-parentheses $(WIN_FLAGS) -c -o $@ $<

build/win/nt_win_shim.cpp.o: emu/nt_win_shim.cpp emu/nt_win_shim.h
	@mkdir -p $(@D)
	$(WINCXX) -std=c++17 $(WIN_FLAGS) -c -o $@ $<

build/win/%.c.o: %.c
	@mkdir -p $(@D)
	$(WINCC) -std=gnu11 $(WIN_FLAGS) -Wno-unused -c -o $@ $<

emu/perseids.dll: $(WIN_OBJ)
	$(WINCXX) -shared -o $@ $^ -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -lpsapi
	x86_64-w64-mingw32-strip $@

win: emu/perseids.dll

# Smoke test of the DLL with a host that exports the NT API like nt_emu.
emu/testhost.exe: emu/testhost.cpp
	$(WINCXX) -std=c++17 -O1 -I$(NT_API_PATH)/include -o $@ $< -static -Wl,--export-all-symbols

wintest: emu/perseids.dll emu/testhost.exe
	cd emu && wine testhost.exe


# --- native tests -------------------------------------------------------------
HOST_FLAGS  := -O2 -g $(MATH) -D__GNUC_PYTHON__ $(DEFS) $(INCS) -I$(NT_EMU_PATH)/fonts
HOST_C      := $(patsubst %,build/host/%.o,$(notdir $(CMSIS_SRC)))

build/host/%.c.o: %.c
	@mkdir -p $(@D)
	gcc -std=gnu11 $(HOST_FLAGS) -Wno-unused -c -o $@ $<

test/sim: test/sim.cpp test/stubs.cpp test/stubs.h $(PLUGIN_SRC) $(ENGINE_SRC) $(HOST_C)
	g++ -std=c++17 -Wall $(HOST_FLAGS) -o $@ test/sim.cpp test/stubs.cpp $(ENGINE_SRC) $(HOST_C)

# Same simulation built like the nt_emu DLL (NT_EMU_WIN: own v[] writes,
# buttons 1/2 switch the pot targets).
test/sim_emu: test/sim.cpp test/stubs.cpp test/stubs.h $(PLUGIN_SRC) $(ENGINE_SRC) $(HOST_C)
	g++ -std=c++17 -Wall -DNT_EMU_WIN $(HOST_FLAGS) -o $@ test/sim.cpp test/stubs.cpp $(ENGINE_SRC) $(HOST_C)

test: test/sim test/sim_emu
	./test/sim
	./test/sim_emu

render/render: render/render.cpp test/stubs.h $(PLUGIN_SRC) $(ENGINE_SRC) $(HOST_C)
	g++ -std=c++17 $(HOST_FLAGS) -DRENDER_LOG -o $@ render/render.cpp $(ENGINE_SRC) $(HOST_C)

render: render/render
	cd render && ./render > frames.txt && python3 render.py frames.txt perseids-screens.png

clean:
	rm -rf build plugins emu/*.dll emu/*.exe test/sim test/sim_emu render/render render/frames.txt

.PHONY: all win wintest test render clean
