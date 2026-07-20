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
#include "mock_i_battery_monitor.hpp"
#include "tank_geometry.hpp"
#include "mock_hal_timer.hpp"
#include "mock_i_ota_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "espnow_ota_trigger.hpp"
#include "ota_controller.hpp"
#include "mock_hal_system.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;
using ::testing::SaveArg;

class WaterTankAppTest : public ::testing::Test {
protected:
    testing::NiceMock<MockLevelSensor> mock_sensor;
    testing::NiceMock<floatswitch::MockFloatSwitch> mock_float_switch;
    testing::NiceMock<MockWaterTankStorage> mock_storage;
    testing::NiceMock<espnow::MockEspNowManager> mock_comm;
    testing::NiceMock<wifi_manager::MockWiFiManager> mock_wifi;
    testing::NiceMock<power_control::MockPowerControl> mock_power;
    testing::NiceMock<idf_hals::MockSleepHAL> mock_sleep;
    testing::NiceMock<battery_monitor::MockBatteryMonitor> mock_battery;
    testing::NiceMock<idf_hals::MockTimerHAL> mock_sys_timer;
    testing::NiceMock<MockOtaManager> mock_ota;
    testing::NiceMock<idf_hals::MockHalFreertos> mock_rtos;
    testing::NiceMock<idf_hals::MockSystemHAL> mock_system_hal;
    EspNowOtaTrigger espnow_ota_trigger;
    OtaController ota_controller{mock_wifi, mock_ota, mock_rtos};
    
    TankGeometry geometry{10}; // offset 10cm (uint8_t)
    WaterTankLogic logic{geometry, mock_float_switch};
    QueueHandle_t dummy_queue = nullptr;
    
    WaterTankApp app{mock_sensor, mock_float_switch, mock_storage, mock_comm, dummy_queue, mock_power, mock_sleep, mock_battery, mock_sys_timer, mock_rtos, logic, espnow_ota_trigger, ota_controller, mock_system_hal};

    void SetUp() override {
        // Default behaviors
        ON_CALL(mock_storage, load(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_storage, save(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_power, turn_off()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, disable_wakeup_source(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, enable_timer_wakeup(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, deep_sleep_start()).WillByDefault(Return());
        ON_CALL(mock_battery, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_battery, read(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_battery, deinit()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_float_switch, should_enable_wakeup()).WillByDefault(Return(false));
    }
};

TEST_F(WaterTankAppTest, Run_ExecutesFullOrchestrationFlow) {
    InSequence s;

    // 1. Load state
    EXPECT_CALL(mock_storage, load(_)).Times(1);

    // 2. Perform sensor reading
    ultrasonic::Reading reading = {ultrasonic::UsResult::OK, 50.0f};
    EXPECT_CALL(mock_sensor, read_level(_)).WillOnce(Return(reading));

    // 3. Turn off power (Energy saving)
    EXPECT_CALL(mock_power, turn_off()).Times(1);

    // 4. Save state
    EXPECT_CALL(mock_storage, save(_)).Times(1);

    // 5. Read battery status
    EXPECT_CALL(mock_battery, init()).Times(1);
    EXPECT_CALL(mock_battery, read(_)).Times(1);
    EXPECT_CALL(mock_battery, deinit()).Times(1);

    // 6. Transmit data
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
    EXPECT_CALL(mock_sensor, read_level(_)).WillOnce(Return(ultrasonic::Reading{ultrasonic::UsResult::OK, 100.0f}));
    EXPECT_CALL(mock_power, turn_off()).Times(1);
    EXPECT_CALL(mock_storage, save(_)).Times(1);
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_sleep, deep_sleep_start()).Times(1);

    app.run();
}

TEST_F(WaterTankAppTest, Run_SleepOverrideCommandSetsSleepDuration) {
    QueueHandle_t mock_queue = reinterpret_cast<QueueHandle_t>(0x1234);
    WaterTankApp app_with_queue{mock_sensor, mock_float_switch, mock_storage, mock_comm, mock_queue, mock_power, mock_sleep, mock_battery, mock_sys_timer, mock_rtos, logic, espnow_ota_trigger, ota_controller, mock_system_hal};

    uint64_t current_time = 0;
    EXPECT_CALL(mock_sys_timer, get_time_us())
        .WillRepeatedly(testing::Invoke([&current_time]() {
            uint64_t ret = current_time;
            current_time += 1000ULL; // Advance 1ms per call
            return ret;
        }));

    // Simulate receiving SLEEP_OVERRIDE command (30 seconds)
    espnow::AppMessage msg{};
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SLEEP_OVERRIDE);
    farm::SleepOverrideCommand cmd{.sleep_time_s = 30};
    memcpy(msg.payload, &cmd, sizeof(cmd));
    msg.payload_len = sizeof(cmd);

    EXPECT_CALL(mock_rtos, queue_receive(mock_queue, _, _))
        .WillOnce(testing::DoAll(
            testing::WithArg<1>([msg](void* pvBuffer) {
                *reinterpret_cast<espnow::AppMessage*>(pvBuffer) = msg;
            }),
            Return(pdPASS)));

    // Verify sleep timer is set to 30 seconds (30,000,000 us) instead of calculated sleep time
    EXPECT_CALL(mock_sleep, enable_timer_wakeup(30000000ULL)).Times(1);
    EXPECT_CALL(mock_sleep, deep_sleep_start()).Times(1);

    app_with_queue.run();
}

TEST_F(WaterTankAppTest, Run_RebootCommandCallsSystemHal) {
    QueueHandle_t mock_queue = reinterpret_cast<QueueHandle_t>(0x1234);
    WaterTankApp app_with_queue{mock_sensor, mock_float_switch, mock_storage, mock_comm, mock_queue, mock_power, mock_sleep, mock_battery, mock_sys_timer, mock_rtos, logic, espnow_ota_trigger, ota_controller, mock_system_hal};

    uint64_t current_time_reboot = 0;
    EXPECT_CALL(mock_sys_timer, get_time_us())
        .WillRepeatedly(testing::Invoke([&current_time_reboot]() {
            uint64_t ret = current_time_reboot;
            current_time_reboot += 1000ULL; // Advance 1ms per call
            return ret;
        }));

    // Simulate receiving REBOOT command
    espnow::AppMessage msg{};
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::REBOOT);
    msg.payload_len = 0;

    EXPECT_CALL(mock_rtos, queue_receive(mock_queue, _, _))
        .WillOnce(testing::DoAll(
            testing::WithArg<1>([msg](void* pvBuffer) {
                *reinterpret_cast<espnow::AppMessage*>(pvBuffer) = msg;
            }),
            Return(pdPASS)))
        .WillRepeatedly(Return(0));

    // Verify system_hal.restart() is called
    EXPECT_CALL(mock_system_hal, restart()).Times(1);

    app_with_queue.run();
}
