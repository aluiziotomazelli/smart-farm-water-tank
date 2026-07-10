#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_float_switch.hpp"

namespace floatswitch {

class MockFloatSwitch : public IFloatSwitch {
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(bool, is_tank_full, (), (override));
    MOCK_METHOD(bool, should_enable_wakeup, (), (override));
    MOCK_METHOD(esp_err_t, get_wakeup_config, (int& gpio_num, bool& wake_on_high), (const, override));
};

} // namespace floatswitch
