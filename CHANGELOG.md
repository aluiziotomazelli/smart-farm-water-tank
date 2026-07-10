# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
