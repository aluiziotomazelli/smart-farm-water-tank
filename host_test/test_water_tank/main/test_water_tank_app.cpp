#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "water_tank_app.hpp"
#include "mock_i_level_sensor.hpp"
#include "mock_i_float_switch.hpp"
#include "mock_i_water_tank_storage.hpp"
#include "mock_i_espnow_manager.hpp"
#include "mock_i_wifi_manager.hpp"
#include "mock_i_power_control.hpp"
#include "mock_hal_sleep.hpp"
#include "tank_geometry.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;
using ::testing::SaveArg;

class WaterTankAppTest : public ::testing::Test {
protected:
    MockLevelSensor mock_sensor;
    floatswitch::MockFloatSwitch mock_float_switch;
    MockWaterTankStorage mock_storage;
    espnow::MockEspNowManager mock_comm;
    wifi_manager::MockWiFiManager mock_wifi;
    power_control::MockPowerControl mock_power;
    MockSleepHAL mock_sleep;
    
    TankGeometry geometry{10}; // offset 10cm (uint8_t)
    WaterTankLogic logic{geometry, mock_float_switch};
    
    WaterTankApp app{mock_sensor, mock_float_switch, mock_storage, mock_comm, mock_wifi, mock_power, mock_sleep, logic};

    void SetUp() override {
        // Default behaviors
        ON_CALL(mock_storage, load(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_storage, save(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_power, turn_off()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, disable_wakeup_source(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, enable_timer_wakeup(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, deep_sleep_start()).WillByDefault(Return());
    }
};

TEST_F(WaterTankAppTest, Run_ExecutesFullOrchestrationFlow) {
    InSequence s;

    // 1. Load state
    EXPECT_CALL(mock_storage, load(_)).Times(1);

    // 2. Perform sensor reading
    ultrasonic::Reading reading = {ultrasonic::UsResult::OK, 50.0f};
    EXPECT_CALL(mock_sensor, read_level()).WillOnce(Return(reading));

    // 3. Turn off power (Energy saving)
    EXPECT_CALL(mock_power, turn_off()).Times(1);

    // 4. Save state
    EXPECT_CALL(mock_storage, save(_)).Times(1);

    // 5. Transmit data
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));

    // 6. Enter deep sleep
    EXPECT_CALL(mock_sleep, disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)).Times(1);
    EXPECT_CALL(mock_sleep, enable_timer_wakeup(_)).Times(1);
    EXPECT_CALL(mock_sleep, deep_sleep_start()).Times(1);
    
    app.run();
}

TEST_F(WaterTankAppTest, Run_HandlesStorageLoadFailure) {
    EXPECT_CALL(mock_storage, load(_)).WillOnce(Return(ESP_FAIL));
    EXPECT_CALL(mock_storage, reset_to_defaults(_)).Times(1);
    
    // Continue the rest of the flow
    EXPECT_CALL(mock_sensor, read_level()).WillOnce(Return(ultrasonic::Reading{ultrasonic::UsResult::OK, 100.0f}));
    EXPECT_CALL(mock_power, turn_off()).Times(1);
    EXPECT_CALL(mock_storage, save(_)).Times(1);
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_sleep, deep_sleep_start()).Times(1);

    app.run();
}
