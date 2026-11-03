import time
import binascii

from pymodbus.client import ModbusSerialClient
from pymodbus.exceptions import ModbusIOException


def trace_packet(send: bool, data: bytes) -> bytes:
    direction = "TX" if send else "RX"
    hex_data = binascii.hexlify(data, " ").decode().upper()
    print(f"{direction}: {hex_data}")
    return data

# Инициализируем наш промышленный serial-клиент
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
    print("Ошибка: Не удалось открыть порт /dev/ttyUSB0")
    exit()

print("Промышленная шина RS-485 запущена. Опрашиваем устройство ID=1...")

try:
    while True:
        # Используем найденный тобой железобетонный аргумент device_id
        response = client.read_holding_registers(address=0x0000, count=2, device_id=1)
        
        if response.isError():
            print(f"Ошибка Modbus: {response}")
        else:
            regs = response.registers
            print(f"Modbus Данные -> Сырое: {regs[0]:04d} | Фильтрованное: {regs[1]:04d}")
            
        time.sleep(0.2)  # Период опроса 200 мс
except KeyboardInterrupt:
    print("\nОпрос завершен.")
finally:
    client.close()
