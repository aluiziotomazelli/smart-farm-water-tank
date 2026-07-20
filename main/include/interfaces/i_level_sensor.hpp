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
     * @brief Reads the distance or level from the sensor.
     *
     * @param sample_count Number of individual measurements (samples) to perform internally.
     *        The sensor implementation uses this to control the accuracy vs. speed trade-off.
     *
     * @return ultrasonic::Reading containing the distance in cm and the result status.
     */
    virtual ultrasonic::Reading read_level(uint8_t sample_count) = 0;
};
