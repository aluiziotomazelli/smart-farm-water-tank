# host_test/test_water_tank_app/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_water_tank_app)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_water_tank_app 
    "*/main/src/*"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_water_tank_app 
    "*/main/include/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_water_tank_app test_water_tank_app.elf)
