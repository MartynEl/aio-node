cmake_minimum_required(VERSION 3.22)

project(aio_node C)

# Strict C standards configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Compilation database for Cppcheck / MISRA

# Default build type configuration
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Project build features configuration options
option(AIO_BUILD_HOST_TESTS "Build host unit tests on PC" ON)
option(AIO_BUILD_HOST_SIMULATOR "Build host interactive simulator" ON)
option(AIO_BUILD_MCU "Build production STM32 firmware" OFF)
option(AIO_WERROR "Treat warnings as errors" ON)

# Hardware isolation protection: block mixed kache states between PC and ARM targets
if(AIO_BUILD_MCU AND (AIO_BUILD_HOST_TESTS OR AIO_BUILD_HOST_SIMULATOR))
    message(FATAL_ERROR
        "MCU build must be configured in a separate directory with: "
        "-DAIO_BUILD_HOST_TESTS=OFF -DAIO_BUILD_HOST_SIMULATOR=OFF")
endif()

if(AIO_BUILD_MCU AND NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "MCU build requires an ARM cross toolchain. "
        "Use -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake")
endif()

# ---------------------------------------------------------------------------
# Core Logic & Services Static Library (Pure Portable standard C)
# ---------------------------------------------------------------------------
add_library(aio_core STATIC
    src/core/filter.c
    src/core/pid.c
    src/service/modbus.c
    src/service/modbus_regs.c
    src/service/wear_leveling.c
)

# Set search include directory paths globally
target_include_directories(aio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Rigorous warning flags enforcement for quality insurance control
target_compile_options(aio_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wdouble-promotion
    -Wformat=2
)

if(AIO_WERROR)
    target_compile_options(aio_core PRIVATE -Werror)
endif()

# ---------------------------------------------------------------------------
# Host Environment Compilation Target (Software-in-the-Loop Simulator)
# ---------------------------------------------------------------------------
if(AIO_BUILD_HOST_SIMULATOR)
    add_executable(aio_host
        src/app/main_host.c
        src/app/app.c
        src/app/scheduler.c
        src/platform/host/platform_host.c
        src/core/platform_stubs.c  # Fixed via architecture review linkage audit
        src/service/logger.c
    )
    target_link_libraries(aio_host PRIVATE aio_core)
    target_compile_options(aio_host PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(AIO_WERROR)
        target_compile_options(aio_host PRIVATE -Werror)
    endif()
endif()

# Connect automated testing directory block
if(AIO_BUILD_HOST_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# MCU Target Environment Compilation Pipeline (Hardware-in-the-Loop Firmware)
# ---------------------------------------------------------------------------
if(AIO_BUILD_MCU)
    # Suppress pedantic noise flags for low-level vector startup drivers
    set_source_files_properties(
        src/platform/stm32f1/platform_stm32f1.c
        PROPERTIES COMPILE_OPTIONS "-Wno-pedantic"
    )

    add_executable(aio_fw
        src/app/main_mcu.c
        src/app/app.c
        src/app/scheduler.c
        src/platform/stm32f1/platform_stm32f1.c
        src/platform/stm32f1/adc_dma.c
        src/platform/stm32f1/pwm_timer.c
        src/platform/stm32f1/startup_stm32f1.c
        src/platform/stm32f1/hal_stubs.c
        src/service/logger.c
        src/platform/stm32f1/sys_tick.c
        src/platform/stm32f1/gpio_led.c
        src/platform/stm32f1/uart1.c
    )
    
    target_link_libraries(aio_fw PRIVATE aio_core)
    target_compile_options(aio_fw PRIVATE -Wall -Wextra -Wshadow)
    if(AIO_WERROR)
        target_compile_options(aio_fw PRIVATE -Werror)
    endif()

    # Hardware target linker map allocation paths parameters
    target_link_options(aio_fw PRIVATE
        -T ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/stm32f1/stm32f103c8tx.ld
        -Wl,--gc-sections
        -Wl,-Map=aio_fw.map
        -Wl,--print-memory-usage
    )

    # Post-Build targets execution block compiling hex and raw bin files binary matrix
    add_custom_command(TARGET aio_fw POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:aio_fw> ${CMAKE_CURRENT_BINARY_DIR}/aio_fw.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:aio_fw> ${CMAKE_CURRENT_BINARY_DIR}/aio_fw.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:aio_fw>
        VERBATIM
    )
endif()
