#!/bin/bash

# Скрипт автоматического развертывания структуры проекта AIO-Node
echo "🚀 Создание структуры каталогов проекта AIO-Node..."

# 1. Создание папок
mkdir -p cmake
mkdir -p docs
mkdir -p include/app
mkdir -p include/core
mkdir -p include/platform
mkdir -p include/service
mkdir -p src/app
mkdir -p src/core
mkdir -p src/platform/host
mkdir -p src/platform/stm32f1
mkdir -p src/service
mkdir -p tests

# 2. Создание пустых файлов конфигурации в корне
touch .clang-format
touch .gitignore
touch CMakeLists.txt
touch misra.json

# 3. Создание файлов документации и сборки
touch cmake/arm-none-eabi.cmake
touch docs/modbus_regs.md
touch tests/CMakeLists.txt

# 4. Создание заголовочных файлов (include)
touch include/app/app.h
touch include/app/scheduler.h
touch include/core/filter.h
touch include/core/pid.h
touch include/core/types.h
touch include/platform/adc_port.h
touch include/platform/pwm_port.h
touch include/platform/platform.h
touch include/service/logger.h
touch include/service/modbus.h
touch include/service/modbus_regs.h
touch include/service/wear_leveling.h

# 5. Создание файлов исходного кода (src)
touch src/app/app.c
touch src/app/main_host.c
touch src/app/main_mcu.c
touch src/app/scheduler.c
touch src/core/filter.c
touch src/core/pid.c
touch src/platform/host/platform_host.c
touch src/platform/stm32f1/platform_stm32f1.c
touch src/platform/stm32f1/adc_dma.c
touch src/platform/stm32f1/pwm_timer.c
touch src/service/logger.c
touch src/service/modbus.c
touch src/service/modbus_regs.c
touch src/service/wear_leveling.c

# 6. Создание файлов тестов (tests)
touch tests/test_filter.c
touch tests/test_pid.c
touch tests/test_wear_leveling.c

echo "✅ Все каталоги и файлы успешно созданы!"
