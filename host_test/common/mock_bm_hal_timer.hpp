// host_test/common/mock_bm_hal_timer.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_bm_hal_timer.hpp"

namespace battery_monitor {

class MockBmHalTimer : public IBmHalTimer {
public:
    MOCK_METHOD(void, delay_us, (uint32_t us), (const, override));
    MOCK_METHOD(int64_t, get_time_us, (), (const, override));
};

} // namespace battery_monitor
