Import("env")

DEBUG = False
MCU = ["-mthumb", "-mfloat-abi=hard", "-mfpu=fpv5-d16", "-mcpu=cortex-m7"]

C_DEFS = [
    "-DCORE_CM7",
    "-DSTM32H750xx",
    "-DSTM32H750IB",
    "-DARM_MATH_CM7",
    "-Dflash_layout",
    "-DHSE_VALUE=16000000",
    "-DUSE_HAL_DRIVER",
    "-DUSE_FULL_LL_DRIVER",
    "-DDATA_IN_D2_SRAM",
    "-DFILEIO_ENABLE_FATFS_READER",
]

C_INCLUDES = [
    "-I$PROJECT_DIR/lib/libDaisy/src",
    "-I$PROJECT_DIR/lib/libDaisy/src/sys",
    "-I$PROJECT_DIR/lib/libDaisy/src/dev/codec_ak4556",
    "-I$PROJECT_DIR/lib/libDaisy/src/dev/sdram",
    "-I$PROJECT_DIR/lib/libDaisy/src/hid/audio",
    "-I$PROJECT_DIR/lib/libDaisy/src/per",
    "-I$PROJECT_DIR/lib/libDaisy/src/per/gpio",
    "-I$PROJECT_DIR/lib/libDaisy/src/per/sai",
    "-I$PROJECT_DIR/lib/libDaisy/src/per/sdmmc",
    "-I$PROJECT_DIR/lib/libDaisy/src/per/tim",
    "-I$PROJECT_DIR/lib/libDaisy/src/sys/system_stm32h7xx",
    "-I$PROJECT_DIR/lib/libDaisy/src/usbd",
    "-I$PROJECT_DIR/lib/libDaisy/src/usbh",
    "-I$PROJECT_DIR/lib/libDaisy/src/util",
    "-I$PROJECT_DIR/lib/libDaisy/Drivers/CMSIS_5/CMSIS/Core/Include",
    "-I$PROJECT_DIR/lib/libDaisy/Drivers/CMSIS/Device/ST/STM32H7xx/Include",
    "-I$PROJECT_DIR/lib/libDaisy/Drivers/STM32H7xx_HAL_Driver/Inc",
    "-I$PROJECT_DIR/lib/libDaisy/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy",
    "-I$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Device_Library/Core/Inc",
    "-I$PROJECT_DIR/lib/libDaisy/Middlewares/Patched/ST/STM32_USB_Device_Library/Class/CDC/Inc",
    "-I$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Core/Inc",
    "-I$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc",
    "-I$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Class/MIDI/Inc",
    "-I$PROJECT_DIR/lib/libDaisy/Middlewares/Third_Party/FatFs/src",
    "-I$PROJECT_DIR/lib/libDaisy/core",
]

WARNINGS = [
    "-Wall",
    "-Wno-attributes",
    "-Wno-strict-aliasing",
    "-Wno-maybe-uninitialized",
    "-Wno-stringop-overflow",
]
CPP_WARNINGS = ["-Wno-register"]

if DEBUG:
    C_DEFS.extend(["-g", "-ggdb"])
    OPT = ["-O0"]
else:
    OPT = ["-O3", "-DNDEBUG=1", "-DRELEASE=1"]

CFLAGS = MCU + ["-std=gnu11"] + C_INCLUDES + C_DEFS + WARNINGS + OPT + [
    "-fasm",
    "-fdata-sections",
    "-ffunction-sections",
    "-finline-functions",
]

CXXFLAGS = MCU + ["-std=gnu++17", "-fno-exceptions", "-fno-rtti"] + C_INCLUDES + C_DEFS + WARNINGS + CPP_WARNINGS + OPT + [
    "-fdata-sections",
    "-ffunction-sections",
    "-finline-functions",
]

env.Append(
    ASFLAGS=MCU + WARNINGS + OPT + ["-fdata-sections", "-ffunction-sections"],
    CFLAGS=CFLAGS,
    CXXFLAGS=CXXFLAGS,
    CPPPATH=[
        "$PROJECT_DIR/lib/libDaisy/src",
        "$PROJECT_DIR/lib/libDaisy/src/sys",
        "$PROJECT_DIR/lib/libDaisy/src/usbd",
        "$PROJECT_DIR/lib/libDaisy/Middlewares/Third_Party/FatFs/src",
        "$PROJECT_DIR/lib/libDaisy/Middlewares/Patched/ST/STM32_USB_Device_Library/Class/CDC/Inc",
        "$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Device_Library/Core/Inc",
        "$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Core/Inc",
        "$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc",
        "$PROJECT_DIR/lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Class/MIDI/Inc",
    ],
)
