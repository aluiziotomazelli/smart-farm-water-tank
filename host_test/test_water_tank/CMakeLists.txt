cmake_minimum_required(VERSION 3.16)

# List extra component directories so IDF can find our components.
list(APPEND EXTRA_COMPONENT_DIRS
    "../../components/smart-farm-common/common"
    "../../components/smart-farm-common/nvs_core"
    "../../components/ultrasonic_sensor"
    "../../components/floatswitch"
    "../../components/power_control"
    "../../components/espnow_manager"
    "../../components/wifi_manager"
    "../common"
    "../gtest"
    
    "$ENV{IDF_PATH}/tools/mocks/driver"  # Official ESP-IDF driver mocks
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"  # Official ESP-IDF driver mocks
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    "$ENV{IDF_PATH}/tools/mocks/esp_event"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
)

set(COMPONENTS 
    "main"
    "common"
    "nvs_core"
    "ultrasonic_sensor"
    "floatswitch"
    "espnow_manager"
    "power_control"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(test_water_tank)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_water_tank "*/main/*")

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_water_tank "*/main/include/*")

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_water_tank test_water_tank.elf)