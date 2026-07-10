# BatteryMonitor Component

[![ESP-IDF Build](https://github.com/aluiziotomazelli/battery-monitor/actions/workflows/build.yml/badge.svg)](https://github.com/aluiziotomazelli/battery-monitor/actions/workflows/build.yml)
[![Host Tests](https://github.com/aluiziotomazelli/battery-monitor/actions/workflows/host_test.yml/badge.svg)](https://github.com/aluiziotomazelli/battery-monitor/actions/workflows/host_test.yml)
[![Coverage](https://img.shields.io/badge/coverage-100%25-orange)](https://aluiziotomazelli.github.io/battery-monitor/index.html)

The `BatteryMonitor` component provides a robust abstraction for monitoring rechargeable battery state, voltage levels, and capacity percentages. It calculates battery voltage using a hardware voltage divider scale, averages out noise via multi-sample ADC readings, applies factory calibration parameters, and classifies battery health/levels into standard states.

---

## Architectural Decisions

This component has been designed according to strict architectural guidelines to ensure testability, safety, and isolation:

1. **Hardware Abstraction Layer (HAL)**:
   All direct ESP-IDF driver code (Oneshot ADC and Calibration APIs) is encapsulated within specific HAL wrappers (`HalAdcOneshot` and `HalAdcCalibration`) implementing abstract interfaces (`IHalAdcOneshot`, `IHalAdcCalibration`). No ESP-IDF header files or APIs leak into the business logic.
   
2. **Single Responsibility Principle (SRP)**:
   - **`AdcBatteryReader`** is solely responsible for low-level interactions (GPIO-to-channel mapping, channel configurations, multi-sample averaging with delay, and applying eFuse calibration schemes).
   - **`BatteryMonitor`** is solely responsible for processing voltage divider formulas, mapping battery levels linearly to charge percentage (0-100%), and classifying battery charge states.
   
3. **Dependency Injection**:
   All dependencies (including the timer delay HAL and low-level ADC reader) are passed via constructors using interface references. This allows compiling and testing the business logic on host platforms (like Linux) by injecting Google Mock objects, isolating tests from real microcontroller hardware.

4. **Target-Conditional Compilation**:
   Since the ESP-IDF `esp_adc` component does not exist when compiling target `linux` for host tests, the component's `CMakeLists.txt` selectively excludes concrete hardware drivers on Linux and relies on conditional preprocessor guards to provide dummy types for the interfaces, preventing compilation failure on host builds.

---

## Complete API Reference

For detailed method signatures, configuration parameters, and types, see [API.md](API.md).

---

## Implementation Example

Here is a typical initialization and usage flow of the component:

```cpp
#include "battery_monitor.hpp"
#include "adc_battery_reader.hpp"
#include "hal_adc_oneshot.hpp"
#include "hal_adc_calibration.hpp"
#include "bm_hal_timer.hpp"

// 1. Define configuration settings
battery_monitor::BatteryAdcConfig adc_cfg = {
    .gpio_num = 3,                 // GPIO Pin connected to divider output
    .sample_count = 16,            // Take 16 samples to average out noise
    .sample_delay_us = 1000,       // Delay 1ms between samples
    .enable_calibration = true     // Use ESP32 eFuse calibration scheme
};

battery_monitor::BatteryMonitorConfig monitor_cfg = {
    .divider_top_ohms = 240000,    // 240k resistor
    .divider_bottom_ohms = 240000, // 240k resistor (1:2 ratio)
    .empty_mv = 3000,              // 3.0V is empty
    .full_mv = 4200,               // 4.2V is full
    .low_mv = 3400,                // 3.4V is low
    .critical_mv = 3200            // 3.2V is critical
};

// 2. Instantiate stateless HAL wrappers
static battery_monitor::HalAdcOneshot oneshot_hal;
static battery_monitor::HalAdcCalibration cali_hal;
static battery_monitor::BmHalTimer timer_hal;

// 3. Instantiate component classes (Injecting dependencies)
static battery_monitor::AdcBatteryReader adc_reader(oneshot_hal, cali_hal, timer_hal, adc_cfg);
static battery_monitor::BatteryMonitor battery_monitor(adc_reader, monitor_cfg);

void app_main() {
    // 4. Initialize component
    if (battery_monitor.init() == ESP_OK) {
        
        // 5. Read battery status
        battery_monitor::BatteryReading reading;
        if (battery_monitor.read(reading) == ESP_OK) {
            printf("Battery: %d mV (%d%%) - State: %d\n", 
                   reading.voltage_mv, 
                   reading.percent, 
                   static_cast<int>(reading.state));
        }
    }
}
```

---

## Unit Testing

This component includes a comprehensive suite of host-based unit tests to verify electrical division mapping, timing accuracy, calibration fallbacks, and battery state transitions.

For instructions on how to run the host tests and generate coverage reports, see [host_test/README.md](host_test/README.md).
