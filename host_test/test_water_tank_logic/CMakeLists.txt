# host_test/test_water_tank_logic/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_water_tank_logic)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_water_tank_logic 
    "*/main/src/water_tank_logic.cpp"
    "*/main/src/tank_geometry.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_water_tank_logic 
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_water_tank_logic test_water_tank_logic.elf)
