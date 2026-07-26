#pragma once
#include <cstdint>
#include "us_types.hpp"

/**
 * @class ILevelSensor
 * @brief Interface for a sensor that measures water level/distance.
 *
 * This abstraction allows the application to work with different sensor technologies
 * (Ultrasonic, Pressure, etc.) without changing the core logic.
 */
class ILevelSensor
{
public:
    virtual ~ILevelSensor() = default;

    /**
     * @brief Initializes the sensor hardware if required.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init() { return ESP_OK; }

    /**
     * @brief Reads the distance or level from the sensor.
     *
     * @return ultrasonic::Reading containing the distance in cm and the result status.
     */
    virtual ultrasonic::Reading read_level() = 0;
};
