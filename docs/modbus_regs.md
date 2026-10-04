# Modbus RTU Register Map — AIO-Node (Module Specification)

**Device Configuration:**
*   **Default Protocol Status:** Modbus RTU Slave, Slave ID = 1 (Configurable)
*   **Baud Rate:** 115200 bps
*   **Data Format:** 8-N-1 (8 Data bits, No parity, 1 Stop bit)
*   **Supported Function Codes:** 
    *   `0x03` — Read Holding Registers
    *   `0x06` — Write Single Register
    *   `0x10` — Write Multiple Registers (Required for 32-bit atomic parameters)

---

## 📊 1. Operational & Process Data (Read-Only: Function Code 0x03)

These registers represent raw and processed physical data collected dynamically via DMA.

| Address | Parameter Name | Data Type | Scale / Unit | Description |
|:---|:---|:---|:---|:---|
| **0x0000** | ADC_CH0_RAW | `uint16_t` | 0..4095 | Raw 12-bit ADC value from Channel 0 (e.g., Pressure Sensor) |
| **0x0001** | ADC_CH0_FILTERED | `uint16_t` | 0..4095 | DSP processed value (Median + EMA filter) |
| **0x0002** | ADC_CH1_RAW | `uint16_t` | 0..4095 | Raw 12-bit ADC value from Channel 1 (e.g., Temperature NTC) |
| **0x0003** | ADC_CH1_FILTERED | `uint16_t` | 0..4095 | DSP processed value (Median + EMA filter) |
| **0x0004** | ADC_CH2_RAW | `uint16_t` | 0..4095 | Raw 12-bit ADC value from Channel 2 (e.g., Calibration Potentiometer) |
| **0x0005** | ADC_CH2_FILTERED | `uint16_t` | 0..4095 | DSP processed value (Median + EMA filter) |
| **0x0006** | PWM_CURRENT_DUTY | `uint16_t` | 0.01 % / (0..10000) | Current active Duty Cycle generated on the hardware output pin |
| **0x0007** | DEVICE_STATE | `uint16_t` | Enum | FSM State (0=INIT, 1=RUN, 2=SAFE_MODE, 3=LOW_POWER) |
| **0x0008** | SYSTEM_FAULT_MASK | `uint16_t` | Bitmask | Bit 0: ADC DMA Error, Bit 1: Flash Wear Error, Bit 2: PVD Warning |
| **0x0009** | CRASH_RESET_COUNT | `uint16_t` | Counter | Number of consecutive Watchdog (IWDG) resets recovered |
| **0x000A** | UPTIME_LO | `uint16_t` | 1 ms / LSW | System uptime counter in milliseconds (Low 16-bit Word) |
| **0x000B** | UPTIME_HI | `uint16_t` | 1 ms / MSW | System uptime counter in milliseconds (High 16-bit Word) |

---

## ⚙️ 2. Configuration Parameters (Read/Write: Function Codes 0x03, 0x06, 0x10)

These parameters are mapped directly to the internal structured block mirrored inside the Flash configuration slot (`device_config_t`). 

*Note: Any successful write operation to these registers automatically recalculates the structured CRC32 and triggers the Wear Leveling service update.*

| Address | Parameter Name | Data Type | Default Value | Scale / Unit | Description |
|:---|:---|:---|:---|:---|:---|
| **0x0100** | CFG_SLAVE_ADDRESS | `uint16_t` | 1 | 1..247 | Modbus hardware Slave ID network address |
| **0x0101** | CFG_FILTER_ALPHA | `uint16_t` | 2000 | Q14 / 0..16384 | Exponential Moving Average smoothing coefficient (e.g., 2000/16384 ≈ 0.12) |
| **0x0102** | CFG_PID_KP | `uint16_t` | 1000 | Q10 Fixed | Proportional gain coefficient |
| **0x0103** | CFG_PID_KI | `uint16_t` | 100 | Q10 Fixed | Integral gain coefficient |
| **0x0104** | CFG_PID_KD | `uint16_t` | 50 | Q10 Fixed | Derivative gain coefficient |
| **0x0105** | CFG_TARGET_SETPOINT | `uint16_t` | 2048 | 0..4095 | Target setpoint value for the PID control feedback loop |
| **0x0106** | CFG_MANUAL_PWM_MODE | `uint16_t` | 0 | 0 or 1 | 0 = Automatic PID execution loop, 1 = Manual fixed PWM output duty |
| **0x0107** | CFG_MANUAL_PWM_DUTY | `uint16_t` | 0 | 0.01 % / (0..10000) | Fixed custom duty cycle applied ONLY if MANUAL_PWM_MODE = 1 |
