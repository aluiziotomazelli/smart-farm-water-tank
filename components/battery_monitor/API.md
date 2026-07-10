# BatteryMonitor Component API

This document provides the reference for the BatteryMonitor component API.

---

## Core Types

### `enum class BatteryState`
Represents the state of the battery based on voltage thresholds.

| Value | Description |
|-------|-------------|
| `UNKNOWN` | Battery state is not determined or reading failed. |
| `CRITICAL` | Battery voltage is critically low (requires immediate action). |
| `LOW` | Battery voltage is low. |
| `NORMAL` | Battery is operating in normal voltage range. |
| `FULL` | Battery is fully charged. |

### `struct BatteryMonitorConfig`
Configuration parameters for the voltage divider and battery thresholds.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `divider_top_ohms` | `uint32_t` | 240000 | Resistor value connected to Battery positive terminal (ohms). |
| `divider_bottom_ohms` | `uint32_t` | 240000 | Resistor value connected to Ground (ohms). |
| `empty_mv` | `uint16_t` | 3000 | Battery voltage representing 0% capacity (millivolts). |
| `full_mv` | `uint16_t` | 4200 | Battery voltage representing 100% capacity (millivolts). |
| `low_mv` | `uint16_t` | 3400 | Battery voltage representing low battery level (millivolts). |
| `critical_mv` | `uint16_t` | 3200 | Battery voltage representing critical battery level (millivolts). |

### `struct BatteryAdcConfig`
Configuration parameters for battery ADC hardware channel and sampling.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `gpio_num` | `int` | 3 | GPIO Pin number connected to the battery divider output. |
| `sample_count` | `uint8_t` | 16 | Number of samples to average for a single reading. |
| `sample_delay_us` | `uint32_t` | 1000 | Delay between consecutive samples in microseconds. |
| `enable_calibration` | `bool` | `true` | Enable/disable ESP32 eFuse calibration scheme for ADC readings. |

### `struct BatteryReading`
Container for the results of a battery measurement.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `voltage_mv` | `uint16_t` | 0 | Compensated battery voltage in millivolts. |
| `adc_mv` | `uint16_t` | 0 | Measured ADC pin voltage in millivolts. |
| `percent` | `uint8_t` | 0 | Estimated charge capacity percentage (0 to 100). |
| `state` | `BatteryState` | `UNKNOWN` | Classified battery charge level state. |

---

## Component Interfaces

### `class IBatteryMonitor`
Abstract interface providing high-level battery status and voltage monitoring.

#### `esp_err_t init()`
Initializes the battery monitor and underlying ADC reader.
* **Returns**: `ESP_OK` on success, or hardware initialization error code.

#### `esp_err_t deinit()`
Deinitializes the battery monitor and cleans up ADC resources.
* **Returns**: `ESP_OK` on success, or hardware deinitialization error code.

#### `esp_err_t read(BatteryReading& out)`
Takes a new battery reading, calculates compensated voltage, linear percentage, and state classification.
* **Parameters**: `out` - Reference to a structure where the reading results will be stored.
* **Returns**: `ESP_OK` on success, `ESP_ERR_INVALID_STATE` if not initialized, or a reading error.

#### `bool is_initialized()`
Checks if the battery monitor is initialized.
* **Returns**: `true` if initialized, `false` otherwise.

---

### `class IAdcBatteryReader`
Abstract interface for the low-level ADC driver reader.

#### `esp_err_t init()`
Sets up the ADC peripheral unit, configures the oneshot channel with 12 dB attenuation, and initializes the calibration scheme if enabled.
* **Returns**: `ESP_OK` on success, or ADC driver error code.

#### `esp_err_t deinit()`
Cleans up oneshot ADC unit and calibration configuration.
* **Returns**: `ESP_OK` on success, or ADC driver error code.

#### `esp_err_t read_adc_mv(uint16_t& out_mv)`
Performs multiple ADC readings, averages the raw values, and translates the raw average to voltage in millivolts (using calibration if available).
* **Parameters**: `out_mv` - Reference to store the calculated millivolt reading.
* **Returns**: `ESP_OK` on success, `ESP_ERR_INVALID_STATE` if not initialized, or read error.

#### `bool is_initialized()`
Checks if the ADC reader driver is initialized.
* **Returns**: `true` if initialized, `false` otherwise.
