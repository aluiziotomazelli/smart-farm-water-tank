idf_component_register(
    SRCS 
        "main.cpp"
        "test_ota_controller.cpp"
        "../../../main/src/ota_controller.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main"
        "../../../components/smart-farm-common/host_test/common"
        "../../../components/idf_hals/mocks"
        "../../../components/ota_manager/host_test/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        ota_manager
        idf_hals
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
