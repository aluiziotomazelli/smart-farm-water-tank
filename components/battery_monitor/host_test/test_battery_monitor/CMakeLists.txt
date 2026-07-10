# host_test/test_battery_monitor/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")

# Define the project name for this test application.
project(test_battery_monitor)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_battery_monitor "*/battery_monitor/src/*.cpp" "*/battery_monitor/include/*.hpp")
set(REMOVE_PATTERNS_test_battery_monitor "*/battery_monitor/include/interfaces/*.hpp")

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_battery_monitor test_battery_monitor.elf)
