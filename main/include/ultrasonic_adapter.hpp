#pragma once
#include "interfaces/i_level_sensor.hpp"
#include "us_sensor.hpp"

/**
 * @class UltrasonicLevelSensorAdapter
 * @brief Adapter for the UltrasonicSensor component to match ILevelSensor interface.
 */
class UltrasonicLevelSensorAdapter : public ILevelSensor
{
public:
    /**
     * @brief Construct a new Ultrasonic Level Sensor Adapter object.
     * @param sensor Reference to the ultrasonic sensor interface.
     */
    explicit UltrasonicLevelSensorAdapter(ultrasonic::IUsSensor& sensor)
        : sensor_(sensor)
    {
    }

    /** @copydoc ILevelSensor::read_level() */
    ultrasonic::Reading read_level(uint8_t sample_count) override
    {
        return sensor_.read_distance(sample_count);
    }

private:
    ultrasonic::IUsSensor& sensor_;
};
