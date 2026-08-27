cmake_minimum_required(VERSION 3.16)
project(water_tank_host_tests NONE)

# Enable CTest for this project
include(CTest)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT EXISTS "${PROJECT_ROOT}/main/include/secrets.hpp")
    if(EXISTS "${PROJECT_ROOT}/main/include/secrets.example.hpp")
        message(STATUS "secrets.hpp not found, creating from secrets.example.hpp")
        configure_file("${PROJECT_ROOT}/main/include/secrets.example.hpp"
                       "${PROJECT_ROOT}/main/include/secrets.hpp" COPYONLY)
    endif()
endif()

# Define the tests for CTest.
add_test(NAME test_water_tank_geometry COMMAND ../test_water_tank_geometry/build/test_water_tank_geometry.elf)
add_test(NAME test_water_tank_logic COMMAND ../test_water_tank_logic/build/test_water_tank_logic.elf)
add_test(NAME test_tank_command_handler COMMAND ../test_tank_command_handler/build/test_tank_command_handler.elf)
add_test(NAME test_ota_controller COMMAND ../test_ota_controller/build/test_ota_controller.elf)
add_test(NAME test_water_tank_app COMMAND ../test_water_tank_app/build/test_water_tank_app.elf)

# Unified Coverage Configuration
find_program(LCOV_PATH lcov REQUIRED)
find_program(GENHTML_PATH genhtml REQUIRED)

# The unified coverage report will be placed in host_test/coverage
set(UNIFIED_COVERAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/coverage")

# This target aggregates coverage data (.gcda files) from all test build directories
add_custom_target(unified_coverage
    COMMAND ${LCOV_PATH} --capture --directory .. --output-file unified_coverage.info --rc branch_coverage=1 --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch,inconsistent,source
    COMMAND ${LCOV_PATH} --extract unified_coverage.info "*/main/src/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    COMMAND ${LCOV_PATH} --remove unified_coverage_filtered.info "*/main/include/interfaces/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    COMMAND ${GENHTML_PATH} unified_coverage_filtered.info --output-directory ${UNIFIED_COVERAGE_DIR} --title "Water Tank Unified Coverage" --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,source
    COMMAND ${LCOV_PATH} --list unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating unified coverage report from all host tests"
)

# Cleanup target to manually clear previous results if needed
add_custom_target(coverage_clean
    COMMAND find .. -name "*.gcda" -delete
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${UNIFIED_COVERAGE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cleaning up coverage data"
)

# Helper target to build all tests at once
add_custom_target(build_all_tests
    COMMAND idf.py -C ../test_water_tank_geometry build
    COMMAND idf.py -C ../test_water_tank_logic build
    COMMAND idf.py -C ../test_tank_command_handler build
    COMMAND idf.py -C ../test_ota_controller build
    COMMAND idf.py -C ../test_water_tank_app build
    COMMENT "Building all test projects using idf.py"
)

# Unified target to build, clean, run all tests and generate coverage
add_custom_target(run_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target build_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target coverage_clean
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND ${CMAKE_COMMAND} --build . --target unified_coverage
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Full test cycle: build, clean, run, and coverage"
)
