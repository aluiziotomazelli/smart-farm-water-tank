# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.1] - 2026-08-27

### Added
- Added nighttime adaptive sleep schedule for `STABLE` and full backup states: 15 minutes (`TIMER_STABLE_NIGHT_US`) between 22:00 and 06:00 (`NIGHT_START_HOUR` / `NIGHT_END_HOUR`), preserving 5 minutes during the day and active evening.
- Added pure boolean `is_night` argument in `WaterTankLogic::calculate_sleep_time_us`.
- Added unit test cases for day vs night sleep intervals in `WaterTankLogicTest`.

### Changed
- Bumped firmware version to `0.4.1`.

## [0.4.0] - 2026-08-27

### Added
- Created `IOtaController` interface and concrete `OtaController` component implementing OTA lifecycle operations (initialization, firmware confirmation, polling download worker, rollback handling, and version retrieval).
- Added `MockOtaController` in `host_test/mocks/` and dedicated unit test suite in `host_test/test_ota_controller/` with 13 test cases.
- Added `UDP_LOG_SERVER_IP` and `UDP_LOG_PORT` network configuration definitions to `secrets.example.hpp`.
- Integrated `test_ota_controller` into root `host_test/CMakeLists.txt` for automated CTest and CI execution.

### Changed
- Refactored `WaterTankApp` to inject `IOtaController&` instead of `IOtaManager&`, removing inline OTA polling loops and error code mappings.
- Replaced hardcoded UDP logger IP and port in `WaterTankApp::init` with definitions from `secrets.hpp`.
- Updated `WaterTankAppTest` fixture and all OTA test cases to use `MockOtaController`.
- Bumped firmware version to `0.3.12`.

## [0.3.11] - 2026-08-27

### Added
- Created `ITankCommandHandler` interface and concrete `TankCommandHandler` component with full support for generic transport commands (`START_OTA`, `REBOOT`) and application commands (`SLEEP_OVERRIDE`, `SYNC_TIME`) with acknowledgement handling.
- Added `MockTankCommandHandler` in `host_test/mocks/` and dedicated unit test suite in `host_test/test_tank_command_handler/`.
- Integrated `test_tank_command_handler` into root `host_test/CMakeLists.txt` for automated CTest and CI execution.

### Changed
- Injected `ITankCommandHandler&` into `WaterTankApp`, decoupling command processing and message listening from application business logic.
- Removed legacy inline command processing methods (`listen_for_messages`, `process_command`, `send_cmd_ack`, `sync_time_from_espnow_packet`) from `WaterTankApp`.
- Updated `WaterTankAppTest` fixture and test cases to use `MockTankCommandHandler`.
- Bumped firmware version to `0.3.11`.

## [0.3.10] - 2026-08-21

### Added
- Integrated `ILedController` status LED indicator on GPIO 8 (D8 on Xiao ESP32-C3) providing non-blocking visual feedback patterns (`BOOT_SUCCESS`, `ERROR_BURST`, `PAIRING_MODE`, `TX_PULSE`, `OTA_UPDATING`).
- Dedicated unit tests for status LED patterns and state transitions in `WaterTankAppTest`.
- Root `host_test/CMakeLists.txt` with CTest multi-suite orchestration (`test_water_tank_geometry`, `test_water_tank_logic`, `test_water_tank_app`), `build_all_tests`, `run_all_tests`, and `unified_coverage`.
- GitHub Actions CI workflows for target firmware compilation (`build.yml`) and host unit tests with coverage (`host_test.yml`).
- Automatic fallback from `secrets.example.hpp` to `secrets.hpp` in CMake configurations for CI builds.

### Changed
- Delegated battery percentage and operating state classification directly to `battery_monitor` component (`BatteryChemistry`).
- Replaced manual battery hysteresis state fields in `WaterTankStats` with read-only telemetry logging fields.
- Modularized `host_test` into distinct test suites with shared mock directories and coverage scripts.
- Updated `README.md` and `DESIGN.md` with CI badges, modern architecture diagrams, pinout tables, and comprehensive test documentation.
- Bumped firmware version to `0.3.10`.

## [0.3.9] - 2026-08-17

### Changed
- Refactored `WaterTankNvs` to inherit from the generic `AppStorage<WaterTankStats, Magic, Version>` CRTP base class in `smart-farm-common`, eliminating local NVS boilerplate and implementation files.
- Decoupled domain struct `WaterTankStats` from storage metadata (`magic`, `version`, `crc`), wrapping it automatically with the new `StorageEnvelope` pattern.
- Migrated `CoreStorage` usage in `WaterTankApp` to pure `CoreData` and separated `process_boot_reasons()` from storage initialization.
- Simplified `init_tank_storage()` and `init_core_storage()` logic utilizing `init_app_data()` / `init()` with automatic fallback to defaults.
- Bumped firmware version to `0.3.9`.

## [0.3.0] - 2026-08-06

### Added
- Multi-sample confirmation window (`FILL_STATE_CONFIRMATIONS_REQUIRED = 2`) for `FillState` transitions to filter out single-sample water turbulence noise.
- Persistent candidate state fields (`pending_fill_state`, `pending_state_count`) in `WaterTankStats` (version 2) stored in RTC RAM.
- Graceful peripheral deactivation (`espnow_.deinit()`) before deep sleep and MCU system restarts (`REBOOT` command).
- ACK confirmation helper `send_cmd_ack()` with 100ms task delay before reboot commands.

### Changed
- Refactored `WaterTankApp::process_command()` into clean command `switch` statements.
- Separated `WaterTankApp::run()` logging into modular step-by-step logs (`Aux sensors`, `Ultrasonic reading`, `Tank state`).
- Removed duplicate component error logging in `WaterTankApp::run()`.

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
