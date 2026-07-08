cmake_minimum_required(VERSION 3.16)

# Define the project name
set(PROJECT_NAME "water_tank")

# Add paths for components (submodules and shared components)
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/smart-farm-common/common"
    "${CMAKE_CURRENT_LIST_DIR}/components/smart-farm-common/nvs_core"
    "${CMAKE_CURRENT_LIST_DIR}/components/ultrasonic_sensor"
    "${CMAKE_CURRENT_LIST_DIR}/components/floatswitch"
    "${CMAKE_CURRENT_LIST_DIR}/components/power_control"
    "${CMAKE_CURRENT_LIST_DIR}/components/espnow_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/wifi_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/ota_manager"
)

# Include the ESP-IDF framework
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(${PROJECT_NAME})
