# Sources of the VCV plug-in besides the Rack glue: the NT host, the Perseids
# NT plug-in (unchanged) and the firmware engines it builds on.
# Set NT (the nt/ folder) and ROOT (repository root) before including.
DAISYSP     := $(ROOT)/lib/DaisySP/Source
CMSIS       := $(ROOT)/lib/libDaisy/Drivers/CMSIS-DSP

PERSEIDS_CXX := $(NT)/vcv/src/nt_host.cpp $(NT)/vcv/src/nt_api.cpp $(NT)/vcv/src/nt_globals.cpp \
                $(NT)/perseids_nt.cpp \
                $(ROOT)/src/capture_engine.cpp $(ROOT)/src/spectra_engine.cpp \
                $(ROOT)/src/swarm_engine.cpp $(ROOT)/src/reso_engine.cpp \
                $(DAISYSP)/Effects/wavefolder.cpp $(DAISYSP)/Filters/svf.cpp
PERSEIDS_C   := $(addprefix $(CMSIS)/Source/, \
                TransformFunctions/arm_rfft_fast_f32.c TransformFunctions/arm_rfft_fast_init_f32.c \
                TransformFunctions/arm_cfft_f32.c TransformFunctions/arm_cfft_init_f32.c \
                TransformFunctions/arm_cfft_radix8_f32.c TransformFunctions/arm_bitreversal2.c \
                BasicMathFunctions/arm_mult_f32.c ComplexMathFunctions/arm_cmplx_mag_f32.c \
                CommonTables/arm_common_tables.c CommonTables/arm_const_structs.c)

PERSEIDS_DEFS := -DPERSEIDS_TRAIL_INT16 -DARM_DSP_CONFIG_TABLES -DARM_FFT_ALLOW_TABLES \
                 -DARM_TABLE_TWIDDLECOEF_F32_1024 -DARM_TABLE_BITREVIDX_FLT_1024 \
                 -DARM_TABLE_TWIDDLECOEF_RFFT_F32_2048 -D__GNUC_PYTHON__
PERSEIDS_INCS := -I$(NT)/distingNT_API/include -I$(ROOT)/include -I$(DAISYSP) -I$(DAISYSP)/Utility \
                 -I$(CMSIS)/Include -I$(CMSIS)/PrivateInclude -I$(NT)/vcv/src -I$(NT)/vcv/src/fonts -I$(NT)/render
# Same float behaviour as the firmware: no errno, fused multiply-add.
PERSEIDS_MATH := -fno-math-errno -ffp-contract=fast
