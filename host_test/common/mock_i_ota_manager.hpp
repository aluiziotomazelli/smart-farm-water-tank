#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_ota_manager.hpp"

class MockOtaManager : public IOtaManager {
public:
    MOCK_METHOD(bool, init, (const OtaConfig& config), (override));
    MOCK_METHOD(void, deinit, (), (override));
    MOCK_METHOD(bool, start_ota, (), (override));
    MOCK_METHOD(void, cancel_ota, (), (override));
    MOCK_METHOD(OtaStatus, get_status, (), (const, override));
    MOCK_METHOD(bool, check_pending_verify, (), (const, override));
    MOCK_METHOD(bool, confirm_app_valid, (), (override));
    MOCK_METHOD(void, rollback_and_reboot, (), (override));
};
