#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_level_sensor.hpp"

class MockLevelSensor : public ILevelSensor {
public:
    MOCK_METHOD(ultrasonic::Reading, read_level, (), (override));
};
