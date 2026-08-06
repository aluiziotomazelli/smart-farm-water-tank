# smart-farm-water-tank

Water level monitoring and pump control application for the Smart Farm project. Built for ESP32-C3 (specifically Xiao ESP32-C3).

## Requirements

- ESP-IDF v5.1.1
- Linux environment for host-based unit testing

## Project Structure

- `main/`: Main application source code and entry point
- `host_test/`: Unit tests and mocks designed to run on the host system (Linux)
- `components/`: Git submodules for core libraries and shared components

## Setup and Building

First, clone with submodules:

```bash
git clone --recursive https://github.com/aluiziotomazelli/smart-farm-water-tank.git
cd smart-farm-water-tank
```

To build the firmware:

```bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32c3
idf.py build
```

## Running Host Tests

To build and run host-based unit tests:

```bash
cd host_test/test_water_tank
. $HOME/esp/esp-idf/export.sh
idf.py --preview set-target linux
idf.py build
./build/test_water_tank.elf
```

## Deploying OTA Firmware

To deploy compiled firmware binaries to the local OTA server (`ota-server.local`):

1. Export ESP-IDF environment variables (the deploy script uses ESP-IDF tooling):
   ```bash
   source $HOME/dev/esp/esp-idf/export.sh
   ```
2. Execute `manage.py` from `~/dev/ota_server`:
   ```bash
   python3 ~/dev/ota_server/manage.py deploy ~/dev/workspaces/smart-farm/smart-farm-water-tank/build/water_tank.bin --host ota-server.local
   ```

## System & Configuration Notes

### Main Task Stack Size
Due to concurrent initialization of `WiFiManager`, `EspNowManager`, UDP Logger, NVS storage backends, and sensors, the peak stack consumption during startup reaches ~3.8 KB.
The main task stack size must be set to at least **8 KB (8192 bytes)**:
```ini
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```
Stack high water mark is monitored at startup and before entering deep sleep via `IHalFreertos::task_get_stack_high_water_mark()` (returns remaining space in bytes).

