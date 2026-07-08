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
