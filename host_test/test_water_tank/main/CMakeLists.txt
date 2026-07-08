idf_component_register(
    SRCS 
        "main.cpp"
        "test_water_tank_geometry.cpp"
        "test_water_tank_logic.cpp"
        "test_water_tank_app.cpp"
        "../../../main/src/tank_geometry.cpp"
        "../../../main/src/water_tank_logic.cpp"
        "../../../main/src/water_tank_app.cpp"
        "../../../main/src/water_tank_nvs.cpp"
    INCLUDE_DIRS 
        "."
        "../../common"
        "../../../main/include"
        "../../../main"
        "../../../components/wifi_manager/include"
        "../../../components/espnow_manager/include"
        "../../../components/power_control/include"
    REQUIRES 
        gtest
        common
        ultrasonic_sensor
        floatswitch
        espnow_manager
        power_control
        nvs_core
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()