// host_test/common/mock_i_ota_trigger.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_ota_trigger.hpp"

class MockOtaTrigger : public IOtaTrigger
{
public:
    MOCK_METHOD(esp_err_t, arm, (IOtaTriggerListener& listener), (override));
    MOCK_METHOD(void, disarm, (), (override));
};
