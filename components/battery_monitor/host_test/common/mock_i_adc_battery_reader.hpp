// components/battery_monitor/host_test/common/mock_i_adc_battery_reader.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_adc_battery_reader.hpp"

namespace battery_monitor {

class MockAdcBatteryReader : public IAdcBatteryReader {
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(esp_err_t, read_adc_mv, (uint16_t& out_mv), (override));
    MOCK_METHOD(bool, is_initialized, (), (const, override));
};

} // namespace battery_monitor
