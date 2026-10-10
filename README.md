# AIO-Node: Asynchronous Industrial DAQ & Load Control Module

An advanced, production-ready embedded software node designed for multi-channel analog data acquisition (DAQ) and precise load power regulation. The firmware is developed from scratch without commercial HAL overhead, targeting the ARM Cortex-M3 architecture (STM32F103) while maintaining full Software-in-the-Loop (SiL) simulation capabilities for host x86_64 environments.

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

## 🛡️ Industrial Reliability & Safety Patterns

To graduate this project from a hobbyist prototype to a commercial-grade asset, several mission-critical industrial patterns were implemented:

1. **Event-Driven Asynchronous I/O (DMA-Driven Concept):**
    *   **Zero-CPU RX Overheads:** Multi-channel analog inputs are continuously scanned and streamed into memory by the hardware DMA controller. The CPU wakes up exclusively via Half-Transfer and Transfer-Complete interrupts to process block averages.
    *   **Zero-Copy Networking:** The Modbus RTU parser operates directly on raw incoming network chunks via steady `const` pointers, eliminating staging buffers and high-latency `memcpy` jitter.
2. **Fault Isolation & Brown-out Management:**
    *   **Hardware Power Line Monitoring:** Integrated Programmable Voltage Detector (PVD). If the system supply rail drops below `2.9V`, a high-priority hardware interrupt (`PVD_IRQHandler`) instantly bypasses the application thread to safely isolate power switches (relays/FETs) at the silicon level.
    *   **Multi-Task Ticket Watchdog Protection:** A specialized bitmask synchronization pattern. The hardware Independent Watchdog (IWDG) is only updated if all key background tasks (DSP, PID, Modbus) explicitly register their execution health flag within the active timing quantum.
3. **Flash Endurance Enhancement (Wear-Leveling):**
    *   Since raw Flash sectors degrade after 10,000 erase cycles, an advanced cyclic allocation layout was developed. Config structures are streamed sequentially into a 1KB page block. Sector erase routines trigger only when the page is completely saturated, extending the hardware endurance up to 300,000+ write cycles.

## 📊 Static Verification & MISRA C compliance

The core mathematical engine (`src/core/`) and protocol services adhere strictly to **MISRA C:2012** defensive programming guidelines. The codebase is thoroughly verified using static code analyzers with a justified **Suppression Policy** managed via `misra.json`:
*   `Rule 15.5` (Single return point) is bypassed for clean mapping tables.
*   `Rule 11.5` & `Rule 8.4` are suppressed to support clean generic object polymorphism (`void *ctx`) and GCC weak linkage overrides (`__attribute__((weak))`) needed for PC-to-MCU hardware substitution.
*   The automated verification script executes via `Cppcheck` and returns **0 errors** for the entire core block.

## 🛠️ Build & Test Instructions

The system features dual independent build configurations managed via CMake. Mixing caches is strictly forbidden to protect cross-compiler integrity.

### Host compilation & Dual-verification (PC SiL)
Configure and execute the test suites and thermodynamic simulator on your local machine:

```bash
# Configure host test suite
cmake -B build -DAIO_BUILD_HOST_TESTS=ON -DAIO_BUILD_HOST_SIMULATOR=ON -DAIO_BUILD_MCU=OFF

# Compile and run automated CTest blocks
cmake --build build -j\$(nproc)
ctest --test-dir build --output-on-failure

# Launch interactive thermodynamic process simulator
./build/aio_host
```
Note: Host tests verify the mathematical correctness of fixed-point algorithms. Ensure build-mcu compiles successfully before flashing to hardware.

### Target Hardware cross-compilation (STM32 HiL)
Generate production hex images for the physical STM32 microchip:
```bash
cmake -B build-mcu -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DAIO_BUILD_MCU=ON -DAIO_BUILD_HOST_TESTS=OFF
cmake --build build-mcu -j\$(nproc)
```
