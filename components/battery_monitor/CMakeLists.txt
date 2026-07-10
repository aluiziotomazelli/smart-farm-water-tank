set(COMPONENT_NAME battery_monitor)

if(IDF_TARGET STREQUAL "linux")
    idf_component_register(
        SRCS 
            "src/battery_monitor.cpp"
            "src/adc_battery_reader.cpp"
            "src/bm_hal_timer.cpp"
        INCLUDE_DIRS 
            "include"
            "include/interfaces"
        REQUIRES
            esp_timer
    )
else()
    idf_component_register(
        SRCS 
            "src/battery_monitor.cpp"
            "src/adc_battery_reader.cpp"
            "src/hal_adc_oneshot.cpp"
            "src/hal_adc_calibration.cpp"
            "src/bm_hal_timer.cpp"
        INCLUDE_DIRS 
            "include"
            "include/interfaces"
        REQUIRES 
            esp_adc
            esp_timer
    )
endif()

if(IDF_TARGET STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
