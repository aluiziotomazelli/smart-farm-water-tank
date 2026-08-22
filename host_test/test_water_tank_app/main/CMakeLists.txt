idf_component_register(
    SRCS 
        "main.cpp"
        "test_water_tank_app.cpp"
        "../../../main/src/tank_geometry.cpp"
        "../../../main/src/water_tank_logic.cpp"
        "../../../main/src/water_tank_app.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main"
        "../../../components/smart-farm-common/host_test/common"
        "../../../components/idf_hals/mocks"
        "../../../components/wifi_manager/include/mocks"
        "../../../components/espnow_manager/include/mocks"
        "../../../components/battery_monitor/host_test/mocks"
        "../../../components/ota_manager/host_test/mocks"
        "../../../components/time_manager/host_test/mocks"
        "../../../components/floatswitch/include/mocks"
        "../../../components/ultrasonic_sensor/include/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        ultrasonic_sensor
        floatswitch
        espnow_manager
        power_control
        battery_monitor
        wifi_manager
        idf_hals
        ota_manager
        time_manager
        udp_logger
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
