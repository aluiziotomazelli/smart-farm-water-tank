// host_test/common/mock_i_battery_monitor.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_battery_monitor.hpp"

namespace battery_monitor {

class MockBatteryMonitor : public IBatteryMonitor {
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(esp_err_t, read, (BatteryReading& out), (override));
    MOCK_METHOD(bool, is_initialized, (), (const, override));
};

} // namespace battery_monitor
