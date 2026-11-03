cd ~/Projects/STM32/aio-node

POOL_HEX=$(arm-none-eabi-nm build-mcu/aio_fw | awk '/g_adc_raw_dma_pool$/{print $1}')

if [ -z "$POOL_HEX" ]; then
  echo "ERROR: g_adc_raw_dma_pool not found in build-mcu/aio_fw"
  exit 1
fi

POOL=$((0x$POOL_HEX))

P0=$(printf 0x%08x "$POOL")
P4=$(printf 0x%08x "$((POOL + 4))")
P8=$(printf 0x%08x "$((POOL + 8))")

echo "g_adc_raw_dma_pool = $P0"

openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
  -c "init" \
  -c "reset run" \
  -c "sleep 1200" \
  -c "halt" \
  -c "echo BASELINE" \
  -c "mdw $P0" -c "mdw $P4" -c "mdw $P8" \
  -c "mdw 0x40020014" -c "mdw 0x40020010" \
  -c "mdw 0x4002000C" -c "mdw 0x40020008" -c "mdw 0x40020000" \
  -c "mdw 0x40012404" -c "mdw 0x40012408" \
  -c "mdw 0x4001242C" -c "mdw 0x40012434" -c "mdw 0x40012400" \
  -c "echo FORCE_ONE_SCAN_PROBE" \
  -c "mww 0x40020008 0x000025A6" \
  -c "mww 0x40020004 0x0000000F" \
  -c "mww 0x4002000C 0x00000003" \
  -c "mww $P0 0x5A5A5A5A" \
  -c "mww $P4 0xA5A5A5A5" \
  -c "mww $P8 0x12345678" \
  -c "mww 0x40020008 0x000025A7" \
  -c "mww 0x40012408 0x00400101" \
  -c "sleep 20" \
  -c "echo AFTER_PROBE" \
  -c "mdw $P0" -c "mdw $P4" -c "mdw $P8" \
  -c "mdw 0x4002000C" -c "mdw 0x40020000" \
  -c "mdw 0x40012408" -c "mdw 0x40012400" \
  -c "exit"