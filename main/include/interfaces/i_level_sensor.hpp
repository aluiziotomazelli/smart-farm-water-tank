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
     * @return ultrasonic::Reading containing the distance in cm and the result status.
     */
    virtual ultrasonic::Reading read_level() = 0;
};
