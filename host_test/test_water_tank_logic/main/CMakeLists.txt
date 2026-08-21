idf_component_register(
    SRCS 
        "main.cpp"
        "test_water_tank_logic.cpp"
        "../../../main/src/water_tank_logic.cpp"
        "../../../main/src/tank_geometry.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main"
        "../../../components/floatswitch/include"
        "../../../components/floatswitch/include/mocks"
        "../../../components/ultrasonic_sensor/include"
        "../../../components/smart-farm-common/include"
    REQUIRES 
        gtest
        smart-farm-common
        floatswitch
        ultrasonic_sensor
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
