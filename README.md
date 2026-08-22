# Smart Farm Water Tank Node

[![ESP-IDF Build](https://github.com/aluiziotomazelli/smart-farm-water-tank/actions/workflows/build.yml/badge.svg)](https://github.com/aluiziotomazelli/smart-farm-water-tank/actions/workflows/build.yml)
[![Host Tests](https://github.com/aluiziotomazelli/smart-farm-water-tank/actions/workflows/host_test.yml/badge.svg)](https://github.com/aluiziotomazelli/smart-farm-water-tank/actions/workflows/host_test.yml)
[![Coverage](https://img.shields.io/badge/coverage-report-blue)](https://aluiziotomazelli.github.io/smart-farm-water-tank/index.html)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A modular, low-power IoT peripheral node based on the **Seeed Studio XIAO ESP32-C3**, designed to accurately monitor water reservoir levels (via ultrasonic distance sensing and geometric tank modeling), detect high-water overflow/backup state (via float switch with deep-sleep GPIO wakeup), monitor battery health, provide visual diagnostics via a status LED, and transmit telemetry via **ESP-NOW** to the central Smart Farm Hub.

---

## 1. Physical Principle & Measurement Model

- **Ultrasonic Level Sensing:** Measures distance from sensor face to water surface using ultrasonic echo timing. Supports multi-sampling (default 11 samples), noise variance rejection, and automatic retries on weak signal or high variance.
- **Non-Linear Geometric Tank Modeling (`TankGeometry`):** Lookup-table (LUT) and piecewise model for stacked cylindrical tank geometry that converts raw distance (cm) into accurate volume (L) and permille ($\text{‰}$) capacity.
- **Auxiliary Float Switch (`FloatSwitch`):** Redundant physical switch detecting full reservoir conditions. Features multi-sample hysteresis filtering (`FILL_STATE_CONFIRMATIONS_REQUIRED = 2`) to eliminate water ripple turbulence noise, and arms GPIO deep sleep wakeup when tank is empty/falling.
- **Battery Health Monitoring (`BatteryMonitor`):** Measures 1S LiFePO4 / Li-Po battery voltage via calibrated ADC divider, tracking percentage and operating state (`NORMAL`, `LOW`, `CRITICAL`).
- **Power Rail Control (`PowerControl`):** Dedicated power gate rail energizing the ultrasonic sensor only during measurement intervals (~50ms warmup + sample duration) to achieve minimal quiescent sleep current.

---

## 2. Hardware Pinout & Schematic (Seeed XIAO ESP32-C3)

### Pinout Table

| Pin     | GPIO      | Constant             | Description                                                   |
| :------ | :-------- | :------------------- | :------------------------------------------------------------ |
| **D0**  | `GPIO 2`  | `FLOAT_SWITCH_GPIO`  | Float switch input with deep-sleep GPIO wakeup trigger        |
| **D1**  | `GPIO 3`  | `BATTERY_LEVEL_GPIO` | ADC battery divider input (ADC1_CH3)                          |
| **D2**  | `GPIO 4`  | `US_TRIG_GPIO`       | Ultrasonic sensor Trigger pulse output                        |
| **D3**  | `GPIO 5`  | `US_ECHO_GPIO`       | Ultrasonic sensor Echo input pulse                            |
| **D4**  | `GPIO 6`  | *(Free)*             | General purpose I/O                                           |
| **D5**  | `GPIO 7`  | *(Free)*             | General purpose I/O                                           |
| **D6**  | `GPIO 21` | *(Free)*             | UART0 TX (Debug serial console)                               |
| **D7**  | `GPIO 20` | *(Free)*             | UART0 RX (Debug serial console)                               |
| **D8**  | `GPIO 8`  | `STATUS_LED_GPIO`    | Status LED visual indicator (diagnostic / state feedback)     |
| **D9**  | `GPIO 9`  | `BOOT_BUTTON_GPIO`   | Hardware BOOT button / manual OTA mode trigger                |
| **D10** | `GPIO 10` | `POWER_GPIO`         | Ultrasonic sensor VCC power gating rail                       |

---

## 3. Architecture & Operational Lifecycle

```
                      +-----------------------------+
                      |         ESP32-C3 Boot       |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      |     WaterTankApp::init()    | (LED task, NVS, WiFi, ESP-NOW)
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      |    Sensor Power Rail ON     | (PowerControl GPIO 10)
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      |  Read Aux Sensors (Bat/FS)  | (During ~50ms sensor warmup)
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Ultrasonic Sample & Logic   | (Level permille & fill state)
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      |   Power OFF Sensor Rail     |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Send Telemetry via ESP-NOW  | (WaterLevelReport + LED TX Pulse)
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Listen Window (200 ms)      | (Process commands: Sync, OTA, Reboot)
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Save State & Stop LED Task  | (AppStorage NVS commit)
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      |      Enter Deep Sleep       | (Timer + FloatSwitch GPIO Wakeup)
                      +-----------------------------+
```

### Status LED Visual Indicator (`ILedController`)

A background FreeRTOS task animates the status LED non-blockingly without delaying telemetry or sleep routines:

| Pattern | Description |
| :--- | :--- |
| `BOOT_SUCCESS` | 2 short pulses (100ms on / 100ms off) confirming healthy boot initialization |
| `ERROR_BURST` | 5 rapid pulses (50ms on / 50ms off) on hardware or communication faults |
| `PAIRING_MODE` | Continuous 200ms blinking while searching / registering with the Hub |
| `TX_PULSE` | Quick 30ms single blip when broadcasting a telemetry report |
| `OTA_UPDATING` | Continuous 100ms blinking during firmware download and flash write |

---

## 4. Telemetry Format (`WaterLevelReport`)

ESP-NOW telemetry payload packed with fixed binary representation:

```cpp
struct WaterLevelReport
{
    uint16_t level_permille;        ///< Water level in permille (0 - 1000 ‰)
    uint32_t volume_liters;         ///< Calculated water volume in liters
    int16_t  distance_cm;           ///< Raw measured distance from sensor to surface (0.1 cm resolution)
    uint16_t battery_mv;            ///< Node battery voltage in millivolts
    uint8_t  battery_percent;       ///< Node battery percentage (0 - 100%)
    farm::BatteryState battery_state;///< Node battery health state (UNKNOWN, CRITICAL, LOW, NORMAL, FULL)
    farm::FillState fill_state;     ///< Tank fill trend (EMPTY, FILLING, DRAINING, FULL, STABLE)
    farm::SensorStatus status;      ///< Sensor hardware status (OK, ERROR_OUT_OF_RANGE, HW_FAULT)
    bool     float_switch_is_full;  ///< Physical float switch state (true = full)
    bool     backup_mode_active;    ///< True if ultrasonic failed and relying on float switch
    uint64_t unix_time;             ///< UTC Epoch timestamp in milliseconds (0 if unsynced)
};
```

---

## 5. Building and Flashing Firmware

### Prerequisites
- ESP-IDF **v5.1+** (v5.5 recommended)

```bash
# Export ESP-IDF environment
source $HOME/dev/esp/esp-idf/export.sh

# Build target firmware
idf.py set-target esp32c3
idf.py build

# Flash and monitor connected device
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 6. Running Host-Based Tests (Linux)

The codebase is fully decoupled with abstract interfaces and GoogleTest / GoogleMock fixtures, allowing 100% of domain logic, geometry calculations, storage envelopes, and application orchestrator workflows to run natively on Linux.

### Running Individual Test Suites

```bash
# Test geometry lookup and volume calculations
cd host_test/test_water_tank_geometry
idf.py build && ./build/test_water_tank_geometry.elf

# Test edge logic, fill state filtering, and sleep duration calculations
cd host_test/test_water_tank_logic
idf.py build && ./build/test_water_tank_logic.elf

# Test complete application lifecycle, ESP-NOW, OTA, LED, and persistence
cd host_test/test_water_tank_app
idf.py build && ./build/test_water_tank_app.elf
```

### Running All Test Suites with CTest

```bash
cd host_test
mkdir -p build && cd build
cmake ..
ctest --output-on-failure
```

### Generating Unified Code Coverage

```bash
cd host_test/build
cmake --build . --target run_all_tests
# HTML coverage report generated in host_test/coverage/index.html
```

---

## 7. System & Configuration Notes

### Main Task Stack Size
Due to concurrent initialization of `WiFiManager`, `EspNowManager`, UDP Logger, NVS storage backends, and sensors, the peak stack consumption during startup reaches ~3.8 KB.
The main task stack size must be set to at least **8 KB (8192 bytes)**:
```ini
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```
Stack high water mark is monitored at startup and before entering deep sleep via `IHalFreertos::task_get_stack_high_water_mark()` (returns remaining space in bytes).

---

## 8. License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

### Third-Party Acknowledgments

- **[ESP-IDF](https://github.com/espressif/esp-idf)**: Copyright (c) Espressif Systems (Shanghai) CO LTD — Licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
- **[Embedded Template Library (ETL)](https://www.etlcpp.com/)**: Copyright (c) John Wellbelove — Licensed under the [MIT License](https://opensource.org/licenses/MIT).
- **[GoogleTest / GoogleMock](https://github.com/google/googletest)**: Copyright (c) Google LLC — Licensed under the [BSD-3-Clause License](https://opensource.org/licenses/BSD-3-Clause).

