cmake_minimum_required(VERSION 3.16)

# Define the project name
set(PROJECT_NAME "water_tank")

# Set default sdkconfig profiles for development if not explicitly passed
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.esp32c3;sdkconfig.defaults.dev")
endif()

# Fallback to secrets.example.hpp if secrets.hpp does not exist (e.g. on CI or fresh clone)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/main/include/secrets.hpp")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/main/include/secrets.example.hpp")
        message(STATUS "secrets.hpp not found, creating from secrets.example.hpp")
        configure_file("${CMAKE_CURRENT_LIST_DIR}/main/include/secrets.example.hpp"
                       "${CMAKE_CURRENT_LIST_DIR}/main/include/secrets.hpp" COPYONLY)
    endif()
endif()

# Add paths for components (submodules and shared components)
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/smart-farm-common"
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
