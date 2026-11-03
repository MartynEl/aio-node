import time
import binascii

from pymodbus.client import ModbusSerialClient
from pymodbus.exceptions import ModbusIOException


def trace_packet(send: bool, data: bytes) -> bytes:
    direction = "TX" if send else "RX"
    hex_data = binascii.hexlify(data, " ").decode().upper()
    print(f"{direction}: {hex_data}")
    return data


PORT = "/dev/ttyUSB0"
# BAUDRATE = 115200
BAUDRATE = 9600
SLAVE_ID = 1

client = ModbusSerialClient(
    port=PORT,
    baudrate=BAUDRATE,
    bytesize=8,
    parity="N",
    stopbits=1,
    timeout=2.0,
    retries=1,
    handle_local_echo=False,
    trace_packet=trace_packet,
)

if not client.connect():
    raise SystemExit(f"Ошибка: не удалось открыть порт {PORT}")

print(f"Порт {PORT} открыт. Опрашиваем устройство ID={SLAVE_ID}...")

try:
    while True:
        try:
            response = client.read_holding_registers(
                address=0x0000,
                count=2,
                device_id=SLAVE_ID,
            )

            if response.isError():
                print(f"Modbus error: {response}")
            else:
                regs = response.registers
                print(
                    f"OK -> raw={regs[0]:04d} ({regs[0]:#06x}) | "
                    f"filtered={regs[1]:04d} ({regs[1]:#06x})"
                )

        except ModbusIOException as e:
            print(f"ModbusIOException: {e}")

        time.sleep(0.5)

except KeyboardInterrupt:
    print("\nОпрос завершён.")

finally:
    client.close()