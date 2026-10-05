# Toolchain specification for ARM Cortex-M3 (STM32F103)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify cross-compiler executables
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Cached paths for post-build binary manipulation tools
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "Objcopy tool")
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "Size tool")

# Enforce static library targets compilation without standard main checks
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Never search for programs on the host system paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Only look for libraries and headers inside cross-compiler directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Hard core architecture initialization flags
# -mcpu=cortex-m3: Target specific core
# -mthumb: Enforce Thumb instruction set (mandatory for Cortex-M)
# -ffunction-sections -fdata-sections: Pack every function into its own sector for linker garbage collection
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections")
# -nostartfiles: Bypass standard OS initialization stubs
# --specs=nosys.specs: Force lightweight standalone system implementation
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostartfiles --specs=nosys.specs")
