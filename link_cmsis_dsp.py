Import("env")
from os.path import join

# Flash-friendly CMSIS-DSP subset for RFFT-512 + mult + cmplx_mag.
# Classic F32 RFFT API (no separate tmpBuf): arm_rfft_fast_f32(S, p, pOut, flag).

cmsis = join(env["PROJECT_DIR"], "lib", "libDaisy", "Drivers", "CMSIS-DSP")

env.Append(
    CPPPATH=[
        join(cmsis, "Include"),
        join(cmsis, "PrivateInclude"),
    ],
    CPPDEFINES=[
        "ARM_MATH_CM7",
        "ARM_DSP_CONFIG_TABLES",
        "ARM_FFT_ALLOW_TABLES",
        "ARM_TABLE_TWIDDLECOEF_F32_256",
        "ARM_TABLE_BITREVIDX_FLT_256",
        "ARM_TABLE_TWIDDLECOEF_RFFT_F32_512",
    ],
)

lite = env.BuildLibrary(
    join("$BUILD_DIR", "cmsis_dsp_lite"),
    cmsis,
    [
        "+<Source/TransformFunctions/arm_rfft_fast_f32.c>",
        "+<Source/TransformFunctions/arm_rfft_fast_init_f32.c>",
        "+<Source/TransformFunctions/arm_cfft_f32.c>",
        "+<Source/TransformFunctions/arm_cfft_init_f32.c>",
        "+<Source/TransformFunctions/arm_cfft_radix8_f32.c>",
        "+<Source/TransformFunctions/arm_bitreversal2.c>",
        "+<Source/BasicMathFunctions/arm_mult_f32.c>",
        "+<Source/ComplexMathFunctions/arm_cmplx_mag_f32.c>",
        "+<Source/CommonTables/arm_common_tables.c>",
        "+<Source/CommonTables/arm_const_structs.c>",
    ],
)

env.Append(LIBS=[lite])
