# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-07-12

### Added
- Debounced hardware OTA trigger (`ButtonOtaTrigger`) and network-activated trigger (`EspNowOtaTrigger`).
- Cooperative cancellation in `OtaController::run_fsm()` inner loop that calls `cancel_ota()` upon stop request.
- Binary semaphore synchronization (`task_done_semaphore_`) for graceful task exit and self-deletion.
- Integrated `idf_hals::IHalFreertos` in `WaterTankApp` and `main.cpp` for mockable FreeRTOS primitives.
- Dynamic stop timeout calculation derived from configured FSM timeouts.
- Unit tests verifying triggers, busy states, cancel behavior, and graceful task deletion.
- Included `smart-farm-common` components in `test_water_tank` coverage report.

### Changed
- Refactored `OtaController` to execute as a non-blocking FreeRTOS task.
- Migrated local sleep adapters to the unified `idf_hals::ISleepHAL` dependency.
- Moved GPIO wakeup bit-shifting and mode mapping logic out of the HAL to the application layer.
- Reduced `LISTEN_WINDOW_MS` to 200 ms for optimized battery consumption during active RX.

### Fixed
- Replaced forced task deletion in `OtaController::stop()` to prevent memory leaks and locked driver mutexes.
- Corrected ESP_LOGE macro compile error in FSM watchdog logs.

## [0.1.0] - 2026-07-10

### Added
- Initial version of Smart Farm Water Tank app.
- Main orchestrator (`WaterTankApp`) using run-to-completion pattern.
- Sensor integration: Ultrasonic (level) and FloatSwitch (backup full/empty level).
- Logic module (`WaterTankLogic`) for permille calculations, sleep timer duration, and error state tracking.
- Geometric tank calculation in `TankGeometry` using a lookup table (LUT) for 5 stacked cylinders.
- Battery monitoring integration (`BatteryMonitor`) via ADC with calibration and hysteresis.
- Over-the-Air (OTA) updates support via `OtaController` using the BOOT button as an initial hardware trigger.
- ESP-NOW communication management (`EspNowManager`) configured to transmit to the HUB node.
- NVS persistence support (`WaterTankNvs`) storing operational stats of the node.
- Deep Sleep management (`SleepHAL`) optimizing power consumption based on level stability.
- Sleep HAL GPIO wakeup integration enabling system wakeup triggered by FloatSwitch state transitions.

### Removed
- Unused GPIO wakeup fields from the Water Tank app statistics.
