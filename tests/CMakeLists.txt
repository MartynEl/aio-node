# Automated unit-testing suite rules execution for AIO-Node Core math

# 1. Target test binary configuration for Digital Signal Processing (DSP) filters
add_executable(test_filter
    test_filter.c
)
target_link_libraries(test_filter PRIVATE aio_core)
add_test(NAME filter COMMAND test_filter)

# 2. Target test binary configuration for discrete PID feedback loop controller
add_executable(test_pid
    test_pid.c
)
target_link_libraries(test_pid PRIVATE aio_core)
add_test(NAME pid COMMAND test_pid)

# 3. Target test binary configuration for Flash memory Wear-Leveling emulator
add_executable(test_wear_leveling
    test_wear_leveling.c
)
target_link_libraries(test_wear_leveling PRIVATE aio_core)
add_test(NAME wear_leveling COMMAND test_wear_leveling)
