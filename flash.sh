#!/bin/bash
# flash.sh - Прошивка aio-node на STM32F103C8T6

ELF_FILE="./build-mcu/aio_fw"

if [ ! -f "$ELF_FILE" ]; then
    echo "ERROR: ELF file not found at $ELF_FILE"
    echo "Run 'cmake --build build-mcu' first."
    exit 1
fi

echo "Flashing $ELF_FILE to STM32..."

# Запускаем OpenOCD в режиме однократной команды
# reset halt -> Останавливаем процессор
# program ... verify reset exit -> Прошиваем, проверяем, сбрасываем и выходим
openocd -f interface/stlink.cfg \
        -f target/stm32f1x.cfg \
        -c "program $ELF_FILE verify reset exit"