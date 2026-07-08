# Host Test Plan: `app_water_tank`

This document defines what is testable on the host (Linux) from the `app_water_tank` firmware,
where the tests should live, and what mocks are required for each target.

---

## Test Location

All tests must reside in `host_test/test_water_tank/`, following the same pattern as the existing
`host_test/test_nvs_core/` project. This keeps the ESP-IDF project structure clean, allows all
host tests to be run via a single `ctest` invocation from `host_test/`, and makes shared mocks
available to all test projects via `host_test/common/`.

**Do not place host tests inside `app_water_tank/`**: that directory is a standalone ESP-IDF
project and mixing host test infrastructure with it complicates the CMake configuration.

---

## Summary Table

| Class | Testable | Mocks Required |
| :--- | :--- | :--- |
| `TankGeometry` | ✅ Fully | None |
| `WaterTankLogic` | ✅ Fully | `MockFloatSwitch` |
| `UltrasonicLevelSensorAdapter` | ✅ Fully (after Option C) | `MockUsSensor` |
| `WaterTankApp::run()` | ✅ Partially | `MockLevelSensor`, `MockFloatSwitch`, `MockWaterTankStorage`, `MockEspNowManager` |
| `WaterTankApp::init()` | ❌ Not testable | Initializes GPIO, WiFi, NVS — hardware-only |
| `WaterTankApp::enter_deep_sleep()` | ❌ Not testable | Calls `esp_deep_sleep_start()` — hardware-only |

---

## Component Details

### 1. `TankGeometry` — No mocks needed

**File:** `app_water_tank/main/src/tank_geometry.cpp`

`TankGeometry` is pure math with no ESP-IDF or FreeRTOS dependencies. Its only include is
`<cstdint>`, which is available on the host. It can be compiled and tested in complete isolation.

**What to test:**
- `calculate_permille()` at LUT boundary values (depth 0, 30, 60, 90, 120, 150 cm).
- Smooth interpolation within each segment (no step jump between adjacent cm values).
- Clamping behavior below 0 and above `max_depth` (returns 1000 and 0, respectively).
- Sensor `offset_cm_` correctly shifts the depth calculation.
- `depth_to_permille()` with mid-segment values for correctness of float interpolation.

**Suggested test file:** `host_test/test_water_tank/main/test_tank_geometry.cpp`

---

### 2. `WaterTankLogic` — Requires `MockFloatSwitch`

**File:** `app_water_tank/main/src/water_tank_logic.cpp`

`WaterTankLogic` depends on two things:
- `TankGeometry` — concrete class with no HAL. Used directly (no mock needed).
- `floatswitch::IFloatSwitch` — interface from the `floatswitch` managed component. Mockable.

The class contains the core business logic: fill state transitions (`FILLING`, `DRAINING`,
`STABLE`), operation mode (backup mode), sleep time calculation, and result counters. This is
the highest-priority target for host testing.

**What to test:**
- `process_reading()` updates `level_permille`, `last_distance_cm`, and `last_result`.
- `update_fill_state()` correctly transitions `STABLE → FILLING` when permille delta exceeds `LEVEL_DELTA_MIN`.
- `update_fill_state()` correctly transitions `STABLE → DRAINING` on negative delta.
- Fill state does not change when delta is below the noise threshold.
- `update_operation_mode()` increments `consecutive_failures` on sensor errors and decrements on recovery.
- `update_operation_mode()` sets `backup_mode_active` after `CONSECUTIVE_FAILURES_THRESHOLD` failures.
- `calculate_sleep_time_us()` returns the correct timer for each combination of `fill_state`,
  `backup_mode_active`, and `float_switch.is_tank_full()`.
- Error counters (`timeout_count`, `echo_stuck_count`, etc.) are incremented correctly per `UsResult`.

**Mock required:** `host_test/common/mock_float_switch.hpp`
- Interface: `floatswitch::IFloatSwitch`
- Header: `app_water_tank/managed_components/floatswitch/include/interfaces/i_float_switch.hpp`

**Suggested test file:** `host_test/test_water_tank/main/test_water_tank_logic.cpp`

---

### 3. `UltrasonicLevelSensorAdapter` — Requires `MockUsSensor`

**File:** `app_water_tank/main/include/ultrasonic_adapter.hpp`

After refactoring `UltrasonicLevelSensorAdapter` to depend on `ultrasonic::IUsSensor` (Option C),
the adapter is fully testable on the host. The concrete `UsSensor` class (which initializes GPIO)
is no longer required.

**What to test:**
- `read_level()` forwards the call to `IUsSensor::read_distance()` with the correct `ping_count`.
- The `Reading` returned by `read_distance()` is passed through unmodified.
- Default `ping_count` of 5 is used when not explicitly set.

**Mock required:** `host_test/common/mock_us_sensor.hpp`
- Interface: `ultrasonic::IUsSensor`
- Header: `app_water_tank/managed_components/ultrasonic_sensor/include/interfaces/i_us_sensor.hpp`

**Suggested test file:** `host_test/test_water_tank/main/test_ultrasonic_adapter.cpp`

---

### 4. `WaterTankApp::run()` — Requires multiple mocks

**File:** `app_water_tank/main/src/water_tank_app.cpp`

The `run()` method orchestrates the full measurement cycle. Since all its dependencies are
accessed via interfaces, the entire flow is mockable on the host. `WaterTankApp::init()` and
`enter_deep_sleep()` are hardware-only and should not be tested in host.

**What to test:**
- On a successful sensor reading, `process_reading()` and `save()` are called.
- On a storage `load()` failure, `reset_to_defaults()` is called as fallback.
- `send_report()` is called with the correct `level_permille` and `status` values.
- `send_report()` correctly maps `UsResult` values to `SensorStatus` via `map_status()`.

**Mocks required:**

| Mock | Interface | Header |
| :--- | :--- | :--- |
| `MockLevelSensor` | `ILevelSensor` | `app_water_tank/main/include/interfaces/i_level_sensor.hpp` |
| `MockFloatSwitch` | `floatswitch::IFloatSwitch` | `managed_components/floatswitch/include/interfaces/i_float_switch.hpp` |
| `MockWaterTankStorage` | `IWaterTankStorage` | `app_water_tank/main/include/interfaces/i_water_tank_storage.hpp` |
| `MockEspNowManager` | `espnow::IEspNowManager` | `managed_components/espnow_manager/include/interfaces/i_espnow_manager.hpp` |

**Suggested test file:** `host_test/test_water_tank/main/test_water_tank_app.cpp`

---

## Shared Mocks Location

All new mocks must be added to `host_test/common/`. This directory is already used by
`test_nvs_core` for `mock_hal_nvs.hpp` and is the canonical location for mocks shared
across test projects.

```
host_test/common/
├── mock_hal_nvs.hpp          (exists)
├── mock_float_switch.hpp     (new)
├── mock_us_sensor.hpp        (new)
├── mock_level_sensor.hpp     (new)
├── mock_water_tank_storage.hpp (new)
└── mock_espnow_manager.hpp   (new)
```

---

## Suggested Directory Structure

```
host_test/
└── test_water_tank/
    ├── CMakeLists.txt
    └── main/
        ├── CMakeLists.txt
        ├── main.cpp
        ├── test_tank_geometry.cpp
        ├── test_water_tank_logic.cpp
        ├── test_ultrasonic_adapter.cpp
        └── test_water_tank_app.cpp
```

---

## Not Testable on Host

| Class / Method | Reason |
| :--- | :--- |
| `WaterTankApp::init()` | Initializes GPIO, WiFi, NVS, and ESP-NOW hardware. |
| `WaterTankApp::enter_deep_sleep()` | Calls `esp_deep_sleep_start()` — terminates execution. |
| `WaterTankStorageAdapter` / `WaterTankNvs` (concrete) | These classes wrap `HalNvs`, an ESP-IDF NVS wrapper. Testing them in isolation would require a `MockHalNvs`, similar to the approach in `test_nvs_core`. **`IWaterTankStorage` itself is an interface and is fully mockable** — it is used as a mock when testing `WaterTankApp::run()` (see section 4 above). |
| `UsSensor` (concrete) | Initializes and drives GPIO pins directly. |
