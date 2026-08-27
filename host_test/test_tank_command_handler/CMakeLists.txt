# host_test/test_tank_command_handler/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_tank_command_handler)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_tank_command_handler 
    "*/main/src/*"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_tank_command_handler 
    "*/main/include/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_tank_command_handler test_tank_command_handler.elf)
