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
     * @param ping_count Number of pings to perform per measurement (default 5).
     */
    UltrasonicLevelSensorAdapter(ultrasonic::IUsSensor& sensor, uint8_t ping_count = 5)
        : sensor_(sensor)
        , ping_count_(ping_count)
    {
    }

    /** @copydoc ILevelSensor::read_level() */
    ultrasonic::Reading read_level() override { return sensor_.read_distance(ping_count_); }

private:
    ultrasonic::IUsSensor& sensor_;
    uint8_t ping_count_;
};
