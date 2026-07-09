Import("env")

env.Append(
    CFLAGS=["-std=gnu11"],
    CCFLAGS=["-finline-functions"],
    CXXFLAGS=[
        "-std=gnu++17",
        "-fno-exceptions",
        "-fno-rtti",
    ],
    CPPPATH=[
        "$PROJECT_DIR/lib/DaisySP/Source",
        "$PROJECT_DIR/lib/DaisySP/Source/Utility",
    ],
)
