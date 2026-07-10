# host_test/test_battery_monitor/main/CMakeLists.txt

idf_component_register(
    SRCS 
        "main.cpp"
        "test_battery_monitor.cpp"
    
    INCLUDE_DIRS
        "."
        "../../common"
        "../../../../../host_test/common"
        
    REQUIRES 
        battery_monitor
        gtest
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()