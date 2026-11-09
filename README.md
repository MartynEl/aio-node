# AIO-Node: Asynchronous Industrial DAQ & Load Control Module

![CI Status](https://github.com)
![License](https://shields.io)
![Latest Tag](https://shields.io)

Embedded firmware for STM32F103 targeting industrial data acquisition and load control. The project demonstrates industrial-style patterns: strict layering, deterministic scheduling, and hardware abstraction without vendor HAL.

## 🏗️ Architectural Layers & Framework Matrix

The project strictly enforces a unidirectional five-layer architecture to ensure clean decoupling, testability, and safety compliant with core architectural best practices:

[ Application Layer ]        --> main_host.c (PC SiL) / main_mcu.c (STM32 HiL)
|
[ Services Layer ]           --> Modbus Engine, Flash Wear Leveling, Ring Logger
|
[ Core Business Logic ]      --> Signal Processing (DSP), Fixed-Point PID Loop
|
[ Platform Abstraction ]     --> Platform interfaces (adc_port.h, pwm_port.h)
|
[ BSP / Hardware Drivers ]   --> Native STM32 Registers / Host Simulation Mocks

*   **Application Layer:** Contains the `Composition Root` (`app.c`). It initializes system state machine blocks and coordinates tasks via a deterministic, non-preemptive cooperative scheduler.
*   **Services Layer:** Houses high-level communication and data preservation engines. It includes a zero-copy Modbus RTU parser, a custom lightweight ring-buffer logger, and an EEPROM emulation layer with Flash wear-leveling algorithms.
*   **Core Business Logic:** A pure, sterile, hardware-agnostic C layer executing physical process math. It includes fixed-point DSP filters and an integer discrete PID regulator using Q-format scaling.
*   **Platform Abstraction (HAL Interfaces):** C-contract headers declaring strict port layouts for peripherals (clocks, power management, ADC streams, and PWM timers).
*   **BSP / Hardware Drivers:** The concrete implementation layer. On the MCU target, it directly manipulates peripheral registers (DMA, ADC, TIM1). On the Host PC, it binds a realistic thermodynamic simulation engine mocking real-world behavior.

## 🏗️ System Architecture Overview

The layered architecture ensures:
* Clear separation of concerns
* High testability
* Easy maintainability
* Scalability for future features

Key architectural principles:
* Strict layering with well-defined interfaces
* Hardware abstraction without vendor HAL
* Deterministic scheduling
* Asynchronous communication handling
* Separation of configuration and logic

## 🔢 Fixed-Point Arithmetic Implementation

The firmware uses fixed-point arithmetic to ensure:
* Deterministic execution time
* Consistent results across different MCUs
* No floating-point overhead
* Predictable precision for signal processing

All calculations are performed in Q-format to maintain accuracy while minimizing CPU load. The math engine includes:
* Filter coefficients calculations
* Signal averaging algorithms
* Modbus CRC16 computation

## 🧪 Test Coverage & CI/CD Pipeline

### Test Cases
The project includes the following test cases:
* `test_filter.c` - filter algorithm verification
* `test_pid.c` - PID controller testing
* `test_wear_leveling.c` - flash wear leveling algorithms

### Automated Testing & CI Workflow
The GitHub Actions pipeline automates:
* **Build Verification** - ensures clean compilation on every push
* **Test Execution** - runs the entire test suite via CTest
* **Static Analysis** - checks for MISRA violations and critical coding errors
* **Quality Assurance** - validates code standards and best practices

### Test Suite Coverage
The test suite includes:
* Unit tests for core logic
* Integration tests for communication stack
* Mock peripheral verification
* Filter algorithm accuracy checks
* PID controller performance validation
* Wear leveling correctness verification

### Quality Metrics
The automated pipeline provides:
* Immediate feedback on code quality
* Objective measure of test coverage
* Static analysis results
* Build stability verification

This comprehensive approach ensures code quality and reliability, providing recruiters with tangible evidence of the project's maturity.
For detailed pipeline configuration, see [.github/workflows/ci.yml](.github/workflows/ci.yml)

## 🛡️ Fault Tolerance Mechanisms

The system implements multiple layers of protection:
* Watchdog supervision with ticket-based monitoring
* Safe mode isolation for critical failures
* Error logging with timestamping
* Automatic recovery procedures
* CRC validation for communication
* Data integrity checks for persistent storage
* Power rail monitoring
* Hardware fault detection

## 📡 Modbus Register Map

| Address | Name | Type | Access | Description |
|---------|------|------|--------|-------------|
| `0x0000` | ADC Raw | uint16 | RO | Raw ADC reading |
| `0x0001` | ADC Filtered | uint16 | RO | Filtered ADC value |
| `0x0002` | Status Flags | uint16 | RO | System status flags |
| `0x0100` | Filter Coeff | uint16 | RW | Filter coefficient |
| `0x0101` | Sample Rate | uint16 | RW | Sampling interval |
| `0x0102` | Logger Enable | bool | RW | Logging activation |

Supported Functions: `0x03` Read Holding Registers, `0x06` Write Single Register.

## 📸 Hardware Test Bench

Real-world validation setup used for debugging and HiL testing. The photo confirms hands-on hardware integration, including peripheral wiring, RS-485 communication and SWD programming.

![HiL test bench: STM32F103 breadboard, MAX3485 RS-485 module, ZK-U-85 USB-RS485 converter, ST-Link V2](assets/aio-node-bench.jpg)

*Key components in the photo:*
- STM32F103 (Blue Pill form-factor) board on a breadboard
- MAX3485 half-duplex RS-485 transceiver module
- USB-to-RS485 converter (ZK-U-85)
- ST-Link V2 programmer/debugger (left on the desk), connected to the board SWD pins via the flat ribbon cable
- 10 kΩ rotary potentiometer on PA0 — analog input validation and manual setpoint simulation during HiL testing
- Dragon figurine — personal bench mascot

## 📊 Resource Footprint (STM32F103C8T6)

These metrics demonstrate efficient memory usage suitable for resource-constrained industrial environments.

| Resource | Usage  | Limit | Utilization |
|----------|--------|-------|-------------|
| Flash    | 9.4 KB | 64 KB | 14.74%      |
| RAM      | 2.5 KB | 20 KB | 12.62%      |

*Note: The low utilization leaves headroom for future feature expansion without refactoring core architecture.*

---

## 🧪 Quality Assurance & Verification

### ✅ Automated Testing (CTest)
The project contains 3 CTest suites:

| CTest suite | Binary | Internal test cases |
|---|---:|---:|
| filter | `test_filter` | 5 |
| pid | `test_pid` | 4 |
| wear_leveling | `test_wear_leveling` | 3 |
| **Total** | **3 suites** | **12 cases** |

`ctest --test-dir build --output-on-failure` reports suite-level results: **3/3 passed** when all internal test cases pass.
---

## 🔍 Real-World Debugging Case: The "Clone" Discovery

One of the most valuable lessons from this project was uncovering a discrepancy between marketing specs and actual hardware capabilities.

**The Issue:** The STM32F103C8T6 chip was marked as having 64 KB of Flash, but during development, we observed unexpected behavior when approaching that limit. Deeper investigation revealed that this specific batch consisted of "cloned" or rebranded chips with only 128 KB of Flash programmed via OTP (One-Time Programmable) fuses, effectively limiting usable space.

**The Impact:** This forced a rigorous optimization pass, reducing the final footprint from ~18 KB to the current 5.7 KB. It also highlighted the importance of not trusting datasheets blindly and validating hardware capabilities empirically.

**Why this matters for employers:** This case shows practical problem-solving skills, the ability to debug at the hardware/firmware boundary, and the discipline to optimize under constraints rather than just "making it work."

---

## 🛡️ Industrial Reliability & Safety Patterns

To graduate this project from a hobbyist prototype to a industrial-grade solution, several production-focused industrial patterns were implemented:

1. **Event-Driven Asynchronous I/O (DMA-Driven Concept):**
    *   **Minimal CPU utilization for RX:** Multi-channel analog inputs are continuously scanned and streamed into memory by the hardware DMA controller. The CPU wakes up exclusively via Half-Transfer and Transfer-Complete interrupts to process block averages.
    *   **Zero-Copy Networking:** The Modbus RTU parser operates directly on raw incoming network chunks via steady `const` pointers, eliminating staging buffers and high-latency `memcpy` jitter.
2. **Fault Isolation & Brown-out Management:**
    *   **Hardware Power Line Monitoring:** Integrated Programmable Voltage Detector (PVD). If the system supply rail drops below `2.9V`, a high-priority hardware interrupt (`PVD_IRQHandler`) instantly bypasses the application thread to safely isolate power switches (relays/FETs) at the silicon level.
    *   **Multi-Task Ticket Watchdog Protection:** A specialized bitmask synchronization pattern. The hardware Independent Watchdog (IWDG) is only updated if all key background tasks (DSP, PID, Modbus) explicitly register their execution health flag within the active timing quantum.
3. **Flash Endurance Enhancement (Wear-Leveling):**
    *   Since raw Flash sectors degrade after 10,000 erase cycles, an advanced cyclic allocation layout was developed. Config structures are streamed sequentially into a 1KB page block. Sector erase routines trigger only when the page is completely saturated, extending the hardware endurance up to 300,000+ write cycles.

## 📊 Static Verification & MISRA C compliance

The core mathematical engine (`src/core/`) and protocol services adhere strictly to **MISRA C:2012** defensive programming guidelines. The codebase is verified with a documented Suppression Policy using static code analyzers with a justified **Suppression Policy** managed via `misra.json`:
*   `Rule 15.5` (Single return point) is bypassed for clean mapping tables.
*   `Rule 11.5` & `Rule 8.4` are suppressed to support clean generic object polymorphism (`void *ctx`) and GCC weak linkage overrides (`__attribute__((weak))`) needed for PC-to-MCU hardware substitution.
*   The automated verification is driven by a `Suppression Policy` managed via [misra.json](misra.json), which documents every justified deviation (e.g., Rule 15.5 for table-driven dispatch, Rule 11.5 for the weak-linkage SiL/HiL substitution pattern). The CI pipeline re-verifies the entire core block on every push.

## 🛠️ Build & Test Instructions

The system features dual independent build configurations managed via CMake. Mixing caches is strictly forbidden to protect cross-compiler integrity.

### Host compilation & Dual-verification (PC SiL)
Compile and run host-side tests for software-in-the-loop verification:

```bash
# Configure host test suite
cmake -B build -DAIO_BUILD_HOST_TESTS=ON -DAIO_BUILD_HOST_SIMULATOR=ON -DAIO_BUILD_MCU=OFF
cmake --build build -j$(nproc)

# Compile and run automated CTest blocks (12 tests: filter×5, pid×4, wear_leveling×3)
ctest --test-dir build --output-on-failure

# Launch interactive thermodynamic process simulator
./build/aio_host
```
Note: Host tests verify the mathematical correctness of fixed-point algorithms. Ensure build-mcu compiles successfully before flashing to hardware.

### Target Hardware cross-compilation (STM32 HiL)
Generate production hex images for the physical STM32 microchip:
```bash
cmake -B build-mcu -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DAIO_BUILD_MCU=ON -DAIO_BUILD_HOST_TESTS=OFF
cmake --build build-mcu --clean-first -j\$(nproc)
```
### Flash to STM32F103C8T6 via ST-Link V2 (requires udev rules for USB access)
Requires udev rules for USB access.
```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program build-mcu/aio_fw verify reset exit"
```

---

## 🔧 Installation

### Python Dependencies
To install required Python packages, run the command specified in the placeholder below.

```bash
# Install all dependencies
sudo apt-get install python3-pip python3-minimalmodbus python3-pymodbus
pip3 install -r requirements.txt
```
### System Requirements
Install the necessary system packages using the commands listed in the placeholder below.

```bash
sudo apt-get install python3-pip
sudo apt-get install python3-minimalmodbus
sudo apt-get install python3-pymodbus
```

### Permissions
For serial port access, you may need to set permissions or add your user to the dialout group. The required commands are in the placeholder below.

```bash
sudo chmod 666 /dev/ttyUSB0
```

---

## 🔌 Usage

### Modbus Logger
The `modbus_logger.py` script allows logging data to a file. The example command for running the script is in the placeholder below.

```bash
python3 modbus_logger.py -p /dev/ttyUSB0 -f data.csv -i 1
```

### Parameters
- `-p, --port` — serial port (e.g., `/dev/ttyUSB0`)
- `-f, --file` — output file name
- `-i, --interval` — logging interval (seconds, default is 1)

---

## 📡 RS-485 Communication
⚠️ Important: Before deployment, verify:
* Correct pin connections
* Proper termination resistors (120Ω)
* Ground loop isolation

### Hardware Interface
Current verified hardware configuration uses a MAX3485 half-duplex RS-485 transceiver module.

Important: the used module has reversed signal labeling relative to the common MCU expectation:

| Module label | Actual MAX3485 function |
|---|---|
| TXD | RO |
| RXD | DI |

Verified connection:

| STM32F103 | MAX3485 module | Direction |
|---|---|---|
| PA9 / USART1_TX | RXD / DI | MCU TX → module input |
| PA10 / USART1_RX | TXD / RO | module output → MCU RX |
| PA11 | EN | controls DE and /RE |
| 3V3 | VCC | power |
| GND | GND | common ground |

RS-485 bus:

| MAX3485 | USB-RS485 converter |
|---|---|
| A | A |
| B | B |
| GND | GND |

Common ground between STM32, MAX3485 and USB-RS485 converter is mandatory.

### Important Notes
- Verified baudrate for Stage 3.0 is **9600 8N1**. 115200 is reserved for later stability testing.
- PA11 is used for RS-485 direction control. PA8 is occupied by TIM1_CH1 PWM and must not be used for MAX3485 EN.
- The module's TXD/RXD labeling is reversed relative to typical expectation: module TXD is RO, module RXD is DI.
- EN must control both DE and /RE for correct half-duplex operation.
- During transmission, firmware raises PA11, waits for USART1 TC, then lowers PA11 to return the transceiver to receive mode.
- The current verification was performed on a HiL bench with STM32F103 Blue Pill, MAX3485 module and USB-RS485 converter.

### Example Output
The sample output from the debug script is in the placeholder below.

```python_output
OK -> raw=0005 (0x0005) | filtered=0004 (0x0004)
TX: 01 03 00 00 00 02 C4 0B
RX: 01 03 04 0D ED 0D ED AC 77
```
Decoded:
  * Slave ID: 0x01
  * Function: 0x03 Read Holding Registers
  * Byte count: 0x04
  * Register 0x0000: 0x0DED = 3565
  * Register 0x0001: 0x0DED = 3565
  * CRC16: valid

Observed potentiometer range on the current HiL bench:
  * raw: approximately 0005..3569
  * filtered: approximately 0004..3565

The theoretical 12-bit ADC range is 0..4095. The observed upper limit is specific to the current potentiometer wiring and bench conditions.

### Verification Status

Stage 3.0 Modbus RTU over RS-485 is functionally verified on the current HiL bench.

Confirmed:

- USART1 RX physical path through PA10.
- USART1 interrupt path with correct vector index for IRQ 37.
- Ring buffer capture and Modbus frame parsing.
- FC=03 response includes mandatory byte count.
- CRC16 validation by pymodbus master.
- RS-485 direction control via PA11.
- Live ADC raw and filtered registers observed through Modbus RTU.
- Stable operation at 9600 8N1 on the current bench.

Planned hardening:

- negative Modbus tests: illegal address, unsupported function, broadcast behavior;
- long-duration polling stress test;
- rapid potentiometer movement stress test;
- host regression test for FC=03 response format;
- optional migration to 115200 after 9600 stability is proven;
- FC=06 configuration write and persistence verification on real hardware.
---

## 📋 Documentation

### Project Structure
- `src/` — source code
- `debug/` — debugging scripts
- `docs/` — documentation
- `tests/` — unit and integration tests
- `cmake/` — build configuration files
- `assets/` — documentation assets
- `requirements.txt` — Python dependencies
- `modbus_logger.py` — data logging script

---

## 🛠️ Known Issues
- Module has reversed pin labeling (documented in RS-485 section)
- Ensure correct baud rate configuration (9600 baud)
- Ensure correct driver installation for ST-Link
- Verify OpenOCD version compatibility
- Check for conflicting USB drivers

---

## 📁 Version History
The list of versions is in the placeholder below.

* v1.2-alpha — RS-485 Modbus communication verified end-to-end (9600 baud, half-duplex)
* v1.1-alpha — Software oversampling added
* v1.0-alpha — Hardware oversampling implemented
* v0.9-alpha — ADC linear range verified

---

## 📦 Deployment Considerations

Before deployment, ensure:
* Correct hardware connections
* Proper termination resistors (120Ω)
* Ground loop isolation
* Power supply stability
* Communication cable quality

Recommended practices:
* Use shielded RS-485 cable
* Implement proper signal grounding
* Verify baud rate settings
* Check device addressing
* Configure network topology

## 📜 License

This project is licensed under the **MIT License**. For more details, see the [LICENSE](LICENSE) file.

Copyright © 2026 MartynEl

The license grants permission to:
* Use the software
* Modify the code
* Distribute the software
* Create derivative works

The above copyright notice and this permission notice must be included in all copies or substantial portions of the Software.

