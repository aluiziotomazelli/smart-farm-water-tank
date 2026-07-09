# Battery Monitor Component Plan

## Summary

Create a reusable ESP-IDF C++ component named `battery_monitor` under
`components/battery_monitor`, following the `floatswitch` and `power_control`
structure: explicit interfaces, constructor injection, thin HAL wrappers,
host-testable logic, and no direct ESP-IDF ADC calls in business logic.

The component will support a 1S 18650 Li-ion battery, nominal 3.7 V and max
4.2 V, measured through the current 240k/240k divider. The selected production
pin is `GPIO_NUM_3`. Add a 100 nF ceramic capacitor from the ADC node, meaning
the junction connected to `GPIO_NUM_3`, to GND, placed close to the XIAO.

ESP-IDF API basis:

- ADC oneshot is appropriate for infrequent on-demand readings and uses
  `adc_oneshot_read()`.
- `adc_oneshot_io_to_channel()` will map GPIO to ADC unit/channel instead of
  hard-coding channel numbers.
- ADC calibration converts raw readings to mV with `adc_cali_raw_to_voltage()`.
- Espressif notes ESP32-C3 ADC noise and recommends bypass capacitance and
  multisampling.

Official references:

- https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/adc/adc_oneshot.html
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/adc/adc_calibration.html

## Key Changes

Component structure:

```text
components/battery_monitor/
├── CMakeLists.txt
├── idf_component.yml
├── include/
│   ├── battery_monitor.hpp
│   ├── battery_monitor_types.hpp
│   ├── adc_battery_reader.hpp
│   ├── hal_adc_oneshot.hpp
│   ├── hal_adc_calibration.hpp
│   └── interfaces/
│       ├── i_battery_monitor.hpp
│       ├── i_battery_adc_reader.hpp
│       ├── i_hal_adc_oneshot.hpp
│       └── i_hal_adc_calibration.hpp
├── src/
│   ├── battery_monitor.cpp
│   ├── adc_battery_reader.cpp
│   ├── hal_adc_oneshot.cpp
│   └── hal_adc_calibration.cpp
├── host_test/
│   ├── CMakeLists.txt
│   ├── gtest/
│   ├── common/
│   │   └── mock_battery_adc_reader.hpp
│   └── test_battery_monitor/
└── test_apps/
    └── test_build/

Workspace root additions:
host_test/common/
├── mock_hal_adc_oneshot.hpp
└── mock_hal_adc_calibration.hpp
```

Public API:

```cpp
namespace battery_monitor {

enum class BatteryState : uint8_t {
    UNKNOWN,
    CRITICAL,
    LOW,
    NORMAL,
    FULL,
};

struct BatteryMonitorConfig {
    uint32_t divider_top_ohms = 240000;
    uint32_t divider_bottom_ohms = 240000;
    uint16_t empty_mv = 3000;
    uint16_t full_mv = 4200;
    uint16_t low_mv = 3400;
    uint16_t critical_mv = 3200;
};
- TODO: Using thresholds without hysteresis can make the battery state to
  flicker when the battery voltage is close to a threshold. This can be fixed
  by adding hysteresis to the thresholds in future version.

struct BatteryAdcConfig {
    int gpio_num = 3;
    uint8_t sample_count = 16;
    uint32_t sample_delay_us = 1000;
    bool enable_calibration = true;
};

struct BatteryReading {
    uint16_t voltage_mv = 0;
    uint16_t adc_mv = 0;
    uint8_t percent = 0;
    BatteryState state = BatteryState::UNKNOWN;
};

class IBatteryMonitor {
public:
    virtual ~IBatteryMonitor() = default;
    virtual esp_err_t init() = 0;
    virtual esp_err_t deinit() = 0;
    virtual esp_err_t read(BatteryReading& out) = 0;
    virtual bool is_initialized() const = 0;
};

}
```

Architecture decisions:

- `BatteryMonitor` owns battery math only: divider compensation, percentage
  estimate, and state classification.
- `AdcBatteryReader` owns ADC lifecycle and sampling, but only calls ESP-IDF
  through injected HAL interfaces.
- `HalAdcOneshot` wraps only `esp_adc/adc_oneshot.h` functions 1:1.
- `HalAdcCalibration` wraps only `esp_adc/adc_cali.h` and scheme functions 1:1.
- Calibration is attempted when enabled. If unsupported, reading still works
  using a documented raw-to-mV fallback based on ADC bit width and configured
  attenuation.
- The component will use `ADC_ATTEN_DB_12` internally for the divided 4.2 V
  battery input, because the ADC node is expected around 2.1 V max.
- Percentage is intentionally simple and deterministic for v1: linear clamp
  between `empty_mv` and `full_mv`.


Integration in this app:

- Add `battery_monitor` to `main/CMakeLists.txt`.
- In `main/main.cpp`, replace the unused `BATTERY_LEVEL_GPIO` wiring with the
  new component on `GPIO_NUM_3`.
- Extend `WaterTankApp` constructor to receive
  `battery_monitor::IBatteryMonitor&`.
- In `WaterTankApp::send_report()`, replace `report.battery_mv = 0` with the
  measured `BatteryReading::voltage_mv`; on read failure, log a warning and keep
  `0` as the protocol-compatible unknown value.
- Do not change `WaterLevelReport`; `battery_mv` already exists in
  `smart-farm-common`.

## Test Plan

Host tests:

- `BatteryMonitor` returns corrected battery voltage from ADC-node mV using the
  240k/240k ratio.
- Percentage clamps to 0 at/below `empty_mv`, 100 at/above `full_mv`, and
  interpolates between.
- State classification returns `CRITICAL`, `LOW`, `NORMAL`, or `FULL` from
  configured thresholds.
- `read()` fails with `ESP_ERR_INVALID_STATE` before `init()`.
- ADC reader initializes by resolving GPIO to ADC unit/channel, creating the
  oneshot unit, configuring the channel, and optionally creating calibration.
- ADC reader averages `sample_count` readings and propagates ESP-IDF errors
  from HAL calls.
- Deinit deletes calibration and ADC handles when they were created.

Build checks:

- Build `components/battery_monitor/test_apps/test_build` for `esp32c3`.
- Build the main project for `esp32c3`.
- Run host tests for the component with the same GTest pattern used by
  `power_control` and `floatswitch`.

## Assumptions

- `GPIO_NUM_3` is the production battery ADC pin, despite the initial note
  mentioning `GPIO_NUM_2`.
- The battery is one 18650 Li-ion cell: 4.2 V max.
- The first version keeps the 240k/240k divider to preserve low sleep current.
- A 100 nF ceramic capacitor will be connected between the ADC divider node,
  same node as `GPIO_NUM_3`, and GND.
- Code, comments, logs, docs, and commit messages will be English and ASCII-only
  per the ESP-IDF project skill.
- Existing direct FreeRTOS/ESP-IDF usage in `main` is pre-existing; the new
  component will not add direct hardware calls outside HAL implementation files.
