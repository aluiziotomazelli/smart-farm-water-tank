#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_hal_sleep.hpp"

class MockSleepHAL : public ISleepHAL {
public:
    MOCK_METHOD(esp_err_t, disable_wakeup_source, (esp_sleep_source_t source), (override));
    MOCK_METHOD(esp_err_t, enable_timer_wakeup, (uint64_t time_in_us), (override));
    MOCK_METHOD(esp_err_t, enable_gpio_wakeup, (int gpio_num, bool wake_on_high), (override));
    MOCK_METHOD(void, deep_sleep_start, (), (override));
};
