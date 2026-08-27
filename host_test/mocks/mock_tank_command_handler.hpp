// host_test/mocks/mock_tank_command_handler.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_tank_command_handler.hpp"

class MockTankCommandHandler : public ITankCommandHandler
{
public:
    MOCK_METHOD(TankCommandProcessResult, process, (uint32_t timeout_ms), (override));
};
