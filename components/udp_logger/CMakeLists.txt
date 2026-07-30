idf_component_register(
    SRCS "udp_logger.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lwip freertos esp_system log esp_ringbuf
)
