#include <csetjmp>
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
#include "mock_i_ota_trigger.hpp"
#include "mock_hal_system.hpp"
#include "mock_nvs_core.hpp"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;

// ---------------------------------------------------------------------------
// Testable subclass — exposes protected members for verification
// ---------------------------------------------------------------------------
class TestableWaterTankApp : public WaterTankApp
{
public:
    using WaterTankApp::WaterTankApp;

    const CoreStorage& get_core_data() const { return core_; }
    const WaterTankStats& get_stats() const { return stats_; }
    bool is_session_healthy() const { return session_healthy_; }
    bool is_pending_firmware_verify() const { return pending_firmware_verify_; }
    bool is_pending_core_commit() const { return pending_core_commit_; }
};

static jmp_buf s_jump_env;

/**
 * Fixture for WaterTankApp tests.
 * Manages mock dependencies and injects them into the WaterTankApp.
 */
class WaterTankAppTest : public ::testing::Test
{
protected:
    NiceMock<MockNvsCore> mock_core_storage;
    NiceMock<MockWaterTankStorage> mock_tank_storage;
    NiceMock<MockLevelSensor> mock_sensor;
    NiceMock<floatswitch::MockFloatSwitch> mock_float_switch;
    NiceMock<espnow::MockEspNowManager> mock_comm;
    NiceMock<power_control::MockPowerControl> mock_power;
    NiceMock<idf_hals::MockSleepHAL> mock_sleep;
    NiceMock<battery_monitor::MockBatteryMonitor> mock_battery;
    NiceMock<idf_hals::MockTimerHAL> mock_sys_timer;
    NiceMock<MockOtaManager> mock_ota;
    NiceMock<idf_hals::MockHalFreertos> mock_rtos;
    NiceMock<MockOtaTrigger> mock_btn_trigger;
    NiceMock<MockOtaTrigger> mock_espnow_trigger;
    NiceMock<idf_hals::MockSystemHAL> mock_system_hal;
    NiceMock<wifi_manager::MockWiFiManager> mock_wifi;

    TankGeometry geometry{10}; // offset 10cm (uint8_t)
    WaterTankLogic logic{geometry, mock_float_switch};
    QueueHandle_t dummy_queue = nullptr;

    std::unique_ptr<TestableWaterTankApp> sut;

    void run_one_cycle()
    {
        EXPECT_CALL(mock_sleep, deep_sleep_start()).Times(testing::AtMost(1)).WillOnce(Invoke([]() {
            std::longjmp(s_jump_env, 1);
        }));
        EXPECT_CALL(mock_system_hal, restart()).Times(testing::AtMost(1)).WillOnce(Invoke([]() {
            std::longjmp(s_jump_env, 2);
        }));

        if (setjmp(s_jump_env) == 0) {
            sut->run(true);
        }
    }

    void SetUp() override
    {
        // Default behaviors to ensure tests don't crash by default if left unconfigured
        ultrasonic::Reading default_reading{};
        default_reading.result = ultrasonic::UsResult::OK;
        default_reading.cm = 50.0f;
        ON_CALL(mock_sensor, read_level(_)).WillByDefault(Return(default_reading));

        ON_CALL(mock_core_storage, load_core(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_core_storage, save_core(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_tank_storage, load_app_data(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_tank_storage, save_app_data(_, _)).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_power, turn_off()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_power, turn_on()).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_sleep, disable_wakeup_source(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, enable_timer_wakeup(_)).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_battery, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_battery, read(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_battery, deinit()).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_float_switch, should_enable_wakeup()).WillByDefault(Return(false));
        ON_CALL(mock_float_switch, is_tank_full()).WillByDefault(Return(false));

        ON_CALL(mock_sys_timer, get_time_us()).WillByDefault(Return(1000ULL));

        ON_CALL(mock_ota, init(_)).WillByDefault(Return(true));
        ON_CALL(mock_wifi, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi, add_credentials(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi, start()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_comm, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_float_switch, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sensor, init()).WillByDefault(Return(ESP_OK));

        // Create the system under test
        sut = std::make_unique<TestableWaterTankApp>(
            mock_core_storage,
            mock_tank_storage,
            mock_sensor,
            mock_float_switch,
            mock_comm,
            dummy_queue,
            mock_power,
            mock_sleep,
            mock_battery,
            mock_sys_timer,
            mock_rtos,
            logic,
            mock_wifi,
            mock_ota,
            mock_btn_trigger,
            mock_espnow_trigger,
            mock_system_hal);
    }

    void TearDown() override { sut.reset(); }
};

// ==============================================================================
// Smoke Test / Initialization
// ==============================================================================

TEST_F(WaterTankAppTest, Init_Success_ConfiguresDependencies)
{
    // 1. Storage is loaded during init
    EXPECT_CALL(mock_core_storage, load_core(_)).Times(1);
    EXPECT_CALL(mock_tank_storage, load_app_data(_)).Times(1);

    // 2. OTA Manager is initialized
    EXPECT_CALL(mock_ota, init(_)).Times(1);

    // Wifi manager is initialized but not connected since is_logging=false
    EXPECT_CALL(mock_wifi, init()).Times(1);

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_OK);
}

// ==============================================================================
// Behavioral Test Placeholders
// ==============================================================================

// --- Boot & Wakeup ---

//  Verify behavior when NVS load fails on first boot (it should load defaults)
TEST_F(WaterTankAppTest, Init_HandlesFirstBoot_CreatesDefaultStorage)
{
    // Storage load returns errors
    ON_CALL(mock_core_storage, load_core(_)).WillByDefault(Return(ESP_FAIL));
    ON_CALL(mock_tank_storage, load_app_data(_)).WillByDefault(Return(ESP_FAIL));

    // Storage init will create default storage and save
    EXPECT_CALL(mock_core_storage, save_core(_, _)).Times(1);
    EXPECT_CALL(mock_tank_storage, save_app_data(_, _)).Times(1);

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
}

TEST_F(WaterTankAppTest, Init_WakeupByTimer_SetsNormalReadingMode)
{
    // Arrange: Mock system reset reason as DEEPSLEEP and wakeup cause as TIMER
    EXPECT_CALL(mock_system_hal, reset_reason()).WillOnce(Return(ESP_RST_DEEPSLEEP));
    EXPECT_CALL(mock_sleep, get_wakeup_cause()).WillOnce(Return(ESP_SLEEP_WAKEUP_TIMER));

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(sut->get_core_data().last_wake, WakeSource::TIMER);
}

TEST_F(WaterTankAppTest, Init_WakeupByGpio_SetsFloatSwitchTriggeredMode)
{
    // Arrange: Mock system reset reason as DEEPSLEEP and wakeup cause as GPIO
    EXPECT_CALL(mock_system_hal, reset_reason()).WillOnce(Return(ESP_RST_DEEPSLEEP));
    EXPECT_CALL(mock_sleep, get_wakeup_cause()).WillOnce(Return(ESP_SLEEP_WAKEUP_GPIO));

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(sut->get_core_data().last_wake, WakeSource::GPIO);
}

TEST_F(WaterTankAppTest, Init_ResetReasonPanic_IncrementsCrashAndBootCount)
{
    // Arrange: Mock load_core to populate initial state
    EXPECT_CALL(mock_core_storage, load_core(_)).WillOnce(testing::Invoke([](CoreStorage& core) {
        core.boot_count = 5;
        core.crash_count = 2;
        return ESP_OK;
    }));

    EXPECT_CALL(mock_system_hal, reset_reason()).WillOnce(Return(ESP_RST_PANIC));

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(sut->get_core_data().boot_count, 6);
    EXPECT_EQ(sut->get_core_data().crash_count, 3);
    EXPECT_EQ(sut->get_core_data().last_wake, WakeSource::CRASH);
    EXPECT_TRUE(sut->is_pending_core_commit());
}

TEST_F(WaterTankAppTest, Init_NormalBoot_IncrementsCyclesSinceNvsCommit)
{
    // Arrange: Mock load_app_data to populate initial cycles count
    EXPECT_CALL(mock_tank_storage, load_app_data(_)).WillOnce(testing::Invoke([](WaterTankStats& stats) {
        stats.cycles_since_nvs_commit = 3;
        return ESP_OK;
    }));

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(sut->get_stats().cycles_since_nvs_commit, 4);
}

TEST_F(WaterTankAppTest, Init_WithLogging_ConfiguresFixedChannelPolicyAndRetriesWifi)
{
    // Arrange: Mock ESP-NOW channel policy set call (called once when logging is enabled)
    EXPECT_CALL(mock_comm, set_channel_policy(espnow::ChannelPolicy::FIXED)).Times(1);

    // Mock initial state as not connected so connect_wifi_with_retry executes loop
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::STARTED));

    // Mock first connect attempt fails, second succeeds
    InSequence seq;
    EXPECT_CALL(mock_wifi, connect(_)).WillOnce(Return(ESP_FAIL));
    EXPECT_CALL(mock_wifi, disconnect(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_wifi, connect(_)).WillOnce(Return(ESP_OK));

    // Act
    esp_err_t ret = sut->init(true);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
}

// --- Sensor & Logic Execution ---

TEST_F(WaterTankAppTest, Run_ExecutesSensorReadings_AndAppliesLogic)
{
    sut->init(false);

    ultrasonic::Reading reading{};
    reading.result = ultrasonic::UsResult::OK;
    reading.cm = 40.0f;

    EXPECT_CALL(mock_sensor, read_level(_)).WillOnce(Return(reading));

    run_one_cycle();

    EXPECT_GT(sut->get_stats().level_permille, 0);
}

TEST_F(WaterTankAppTest, Run_HandlesSensorReadFailure_AndRetries)
{
    sut->init(false);

    ultrasonic::Reading weak_reading{};
    weak_reading.result = ultrasonic::UsResult::WEAK_SIGNAL;
    weak_reading.cm = 0.0f;

    ultrasonic::Reading ok_reading{};
    ok_reading.result = ultrasonic::UsResult::OK;
    ok_reading.cm = 50.0f;

    InSequence seq;
    // Initial read (default 11 samples)
    EXPECT_CALL(mock_sensor, read_level(11)).WillOnce(Return(weak_reading));
    // Retry with 1.4x sample count (15 samples: 11 * 1.4 = 15)
    EXPECT_CALL(mock_sensor, read_level(15)).WillOnce(Return(ok_reading));

    run_one_cycle();

    EXPECT_GT(sut->get_stats().level_permille, 0);
}

TEST_F(WaterTankAppTest, Run_PowerOnSensorFail_SetsHardwareFaultAndUnhealthySession)
{
    sut->init(false);

    EXPECT_CALL(mock_power, turn_on()).WillOnce(Return(ESP_FAIL));

    run_one_cycle();

    EXPECT_EQ(sut->get_stats().last_result, ultrasonic::UsResult::HW_FAULT);
    EXPECT_FALSE(sut->is_session_healthy());
}

// --- Communication & Reporting ---

TEST_F(WaterTankAppTest, Run_ConstructsAndSendsReportViaEspNow)
{
    sut->init(false);

    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));

    run_one_cycle();
}

TEST_F(WaterTankAppTest, Run_WhenRecoveryScan_WaitsCommReadyAndRetriesReportSend)
{
    sut->init(false);

    // Expect 2 report transmissions (initial attempt + retry after channel recovery)
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).Times(2).WillRepeatedly(Return(ESP_OK));

    InSequence seq;
    // Checked at line 229 of run()
    EXPECT_CALL(mock_comm, get_node_state()).WillOnce(Return(espnow::NodeState::RECOVERY_SCAN));
    // Polled inside wait_for_comm_ready() loop
    EXPECT_CALL(mock_comm, get_node_state()).WillOnce(Return(espnow::NodeState::RECOVERY_SCAN));
    EXPECT_CALL(mock_comm, get_node_state()).WillOnce(Return(espnow::NodeState::OPERATIONAL));

    run_one_cycle();
}

// --- OTA Management ---

TEST_F(WaterTankAppTest, Init_PendingFirmwareVerify_MarksPartitionValid_WhenSessionHealthy)
{
    // Arrange: Mock pending OTA verification state
    EXPECT_CALL(mock_ota, check_pending_verify()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota, confirm_app_valid()).WillOnce(Return(true));
    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillRepeatedly(Return(ESP_OK));

    // Act: init and run
    esp_err_t ret = sut->init(false);
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_TRUE(sut->is_pending_firmware_verify());

    run_one_cycle();
}

TEST_F(WaterTankAppTest, Run_RollsBackFirmware_WhenSessionNotHealthy)
{
    // Arrange: Mock pending_verify state and healthy = false
    EXPECT_CALL(mock_ota, check_pending_verify()).WillOnce(Return(true));

    // Make session unhealthy by failing WiFi initialization (or another component after wifi is connected/started)
    // To test disconnect_stop_wifi, wifi get_state must be != UNINITIALIZED and != INITIALIZED
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::CONNECTED_GOT_IP));
    EXPECT_CALL(mock_wifi, init()).WillOnce(Return(ESP_FAIL));

    // Check firmware will call wait_for_comm_ready -> comm_.get_node_state()
    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));

    // Then disconnect_stop_wifi() will be called
    EXPECT_CALL(mock_wifi, disconnect(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_wifi, stop(_)).WillOnce(Return(ESP_OK));

    // We expect rollback to be called because pending_firmware_verify_ = true and session_healthy_ = false
    EXPECT_CALL(mock_ota, rollback_and_reboot()).Times(1);

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_FAIL);
}

TEST_F(WaterTankAppTest, OnOtaTriggered_EntersWifiListeningMode)
{
    sut->init(false);
    sut->on_ota_triggered(OtaTriggerSource::ESPNOW);

    EXPECT_CALL(mock_comm, deinit()).Times(1);
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::CONNECTED_GOT_IP));
    EXPECT_CALL(mock_ota, start_ota()).Times(1);
    EXPECT_CALL(mock_ota, get_status()).WillRepeatedly(Return(OtaStatus::READY_TO_RESTART));

    run_one_cycle();
}

// --- Deep Sleep ---

TEST_F(WaterTankAppTest, Run_ConfiguresSleepWakeups_AndEntersDeepSleep)
{
    sut->init(false);

    EXPECT_CALL(mock_float_switch, should_enable_wakeup()).WillOnce(Return(true));
    EXPECT_CALL(mock_float_switch, get_wakeup_config(_, _))
        .WillOnce(DoAll(SetArgReferee<0>(4), SetArgReferee<1>(true), Return(ESP_OK)));

    EXPECT_CALL(mock_sleep, disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_sleep, deep_sleep_enable_gpio_wakeup(1ULL << 4, idf_hals::GpioWakeupMode::HIGH_LEVEL))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_sleep, enable_timer_wakeup(_)).WillOnce(Return(ESP_OK));

    run_one_cycle();
}
