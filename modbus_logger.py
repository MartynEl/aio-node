import minimalmodbus
import time
import argparse

def log_modbus_data(port, filename, interval=1):
    instrument = minimalmodbus.Instrument(port, 1)
    instrument.serial.baudrate = 9600
    
    with open(filename, 'w') as f:
        f.write("Timestamp,Raw,Filtered\n")
        
        try:
            while True:
                raw = instrument.read_register(0, functioncode=3)
                filtered = instrument.read_register(1, functioncode=3)
                timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
                
                f.write(f"{timestamp},{raw},{filtered}\n")
                f.flush()
                time.sleep(interval)
                
        except KeyboardInterrupt:
            print("Logging stopped")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Modbus data logger")
    parser.add_argument('-p', '--port', required=True, help='Serial port (e.g. /dev/ttyUSB0)')
    parser.add_argument('-f', '--file', required=True, help='Output filename')
    parser.add_argument('-i', '--interval', type=float, default=1, help='Logging interval (seconds)')
    
    args = parser.parse_args()
    log_modbus_data(args.port, args.file, args.interval)
