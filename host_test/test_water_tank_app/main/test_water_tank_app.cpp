#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "water_tank_app.hpp"
#include "mock_tank_command_handler.hpp"
#include "mock_i_level_sensor.hpp"
#include "mock_float_switch.hpp"
#include "mock_i_water_tank_storage.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_i_wifi_manager.hpp"
#include "mock_i_power_control.hpp"
#include "mock_hal_sleep.hpp"
#include "mock_i_battery_monitor.hpp"
#include "tank_geometry.hpp"
#include "mock_hal_timer.hpp"
#include "mock_ota_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_i_ota_trigger.hpp"
#include "mock_hal_system.hpp"
#include "mock_nvs_core.hpp"
#include "mock_time_manager.hpp"
#include "mock_led_controller.hpp"

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

    const CoreData& get_core_data() const { return core_; }
    const WaterTankStats& get_stats() const { return stats_; }
    bool is_session_healthy() const { return session_healthy_; }
    bool is_pending_core_commit() const { return pending_core_commit_; }

    void set_session_healthy(bool healthy) { session_healthy_ = healthy; }

    bool call_wait_for_comm_ready(uint32_t timeout_ms) { return wait_for_comm_ready(timeout_ms); }
    void call_process_pending_ota() { process_pending_ota(); }
    void call_check_firmware_healthy() { check_firmware_healthy(); }
};

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
    NiceMock<MockTankCommandHandler> mock_command_handler;
    NiceMock<power_control::MockPowerControl> mock_power;
    NiceMock<idf_hals::MockSleepHAL> mock_sleep;
    NiceMock<battery_monitor::MockBatteryMonitor> mock_battery;
    NiceMock<idf_hals::MockTimerHAL> mock_sys_timer;
    NiceMock<MockOtaManager> mock_ota;
    NiceMock<idf_hals::MockHalFreertos> mock_rtos;
    NiceMock<MockOtaTrigger> mock_btn_trigger;
    NiceMock<idf_hals::MockSystemHAL> mock_system_hal;
    NiceMock<wifi_manager::MockWiFiManager> mock_wifi;
    NiceMock<time_manager::MockTimeManager> mock_time_manager;
    NiceMock<MockLedController> mock_led_controller;

    TankGeometry geometry{10}; // offset 10cm (uint8_t)
    WaterTankLogic logic{geometry, mock_float_switch};
    QueueHandle_t dummy_queue = nullptr;
    uint64_t fake_time_us = 1000ULL;

    std::unique_ptr<TestableWaterTankApp> sut;

    void SetUp() override
    {
        // Default behaviors to ensure tests don't crash by default if left unconfigured
        ultrasonic::Reading default_reading{};
        default_reading.result = ultrasonic::UsResult::OK;
        default_reading.cm = 50.0f;
        ON_CALL(mock_sensor, read_level(_)).WillByDefault(Return(default_reading));

        ON_CALL(mock_core_storage, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_core_storage, save_core(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_tank_storage, init_app_data(_, _)).WillByDefault(Return(ESP_OK));
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

        ON_CALL(mock_sys_timer, get_time_us()).WillByDefault(Invoke([this]() {
            uint64_t ret = fake_time_us;
            fake_time_us += 50000ULL; // Advance 50ms per call
            return ret;
        }));

        ON_CALL(mock_ota, init(_)).WillByDefault(Return(true));
        ON_CALL(mock_wifi, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_time_manager, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(Const(mock_time_manager), is_synchronized()).WillByDefault(Return(true));
        ON_CALL(mock_wifi, add_credentials(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi, start()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_comm, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_float_switch, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sensor, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_command_handler, process(_)).WillByDefault(Return(TankCommandProcessResult{}));

        // Create the system under test
        sut = create_app_with_queue(dummy_queue, /*auto_init=*/false);
    }

    std::unique_ptr<TestableWaterTankApp>
    create_app_with_queue(QueueHandle_t rx_queue, bool auto_init = true)
    {
        auto app = std::make_unique<TestableWaterTankApp>(
            mock_core_storage,
            mock_tank_storage,
            mock_sensor,
            mock_float_switch,
            mock_comm,
            rx_queue,
            mock_command_handler,
            mock_power,
            mock_sleep,
            mock_battery,
            mock_sys_timer,
            mock_rtos,
            logic,
            mock_wifi,
            mock_ota,
            mock_btn_trigger,
            mock_system_hal,
            mock_time_manager,
            mock_led_controller);
        if (auto_init) {
            app->init(false);
        }
        return app;
    }

    void TearDown() override { sut.reset(); }
};

// ==============================================================================
// Smoke Test / Initialization
// ==============================================================================

TEST_F(WaterTankAppTest, Init_Success_ConfiguresDependencies)
{
    // 1. Storage is loaded during init
    EXPECT_CALL(mock_core_storage, init(_, _)).Times(1);
    EXPECT_CALL(mock_tank_storage, init_app_data(_, _)).Times(1);

    // 2. OTA Manager is initialized
    EXPECT_CALL(mock_ota, init(_)).Times(1);

    // Wifi manager is initialized but not connected since is_logging=false
    EXPECT_CALL(mock_wifi, init(_)).Times(1);

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_OK);
}

// ==============================================================================
// Init Failure Tests
// ==============================================================================

TEST_F(WaterTankAppTest, Init_OtaManagerFail_ReturnsErrorImmediately)
{
    EXPECT_CALL(mock_ota, init(_)).WillOnce(Return(false));
    EXPECT_CALL(mock_wifi, init(_)).Times(0);

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_FAIL);
}

TEST_F(WaterTankAppTest, Init_FloatSwitchFail_SetsUnhealthySessionAndReturnsError)
{
    EXPECT_CALL(mock_float_switch, init()).WillOnce(Return(ESP_FAIL));

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_FAIL);
    EXPECT_FALSE(sut->is_session_healthy());
}

TEST_F(WaterTankAppTest, Init_CoreStorageFail_SetsUnhealthySessionAndReturnsError)
{
    ON_CALL(mock_core_storage, init(_, _)).WillByDefault(Return(ESP_FAIL));

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_FAIL);
    EXPECT_FALSE(sut->is_session_healthy());
}

TEST_F(WaterTankAppTest, Init_TankStorageFail_SetsUnhealthySessionAndReturnsError)
{
    ON_CALL(mock_tank_storage, init_app_data(_, _)).WillByDefault(Return(ESP_FAIL));

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_FAIL);
    EXPECT_FALSE(sut->is_session_healthy());
}

TEST_F(WaterTankAppTest, Init_EspNowFail_SetsUnhealthySessionAndReturnsError)
{
    EXPECT_CALL(mock_comm, init(_)).WillOnce(Return(ESP_FAIL));

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_FAIL);
    EXPECT_FALSE(sut->is_session_healthy());
}

TEST_F(WaterTankAppTest, Init_PowerControlFail_SkipsSensorInitAndReturnsError)
{
    EXPECT_CALL(mock_power, init()).WillOnce(Return(ESP_FAIL));
    EXPECT_CALL(mock_sensor, init()).Times(0);

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_FAIL);
    EXPECT_FALSE(sut->is_session_healthy());
}

TEST_F(WaterTankAppTest, Init_SensorFail_SetsUnhealthySessionAndReturnsError)
{
    EXPECT_CALL(mock_sensor, init()).WillOnce(Return(ESP_FAIL));

    esp_err_t ret = sut->init(false);

    EXPECT_EQ(ret, ESP_FAIL);
    EXPECT_FALSE(sut->is_session_healthy());
}

// ==============================================================================
// Behavioral Test Placeholders
// ==============================================================================

// --- Boot & Wakeup ---

//  Verify behavior when NVS load fails on first boot (it should load defaults)
TEST_F(WaterTankAppTest, Init_HandlesFirstBoot_CreatesDefaultStorage)
{
    EXPECT_CALL(mock_core_storage, init(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_tank_storage, init_app_data(_, _)).WillOnce(Return(ESP_OK));

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
}

TEST_F(WaterTankAppTest, Init_WakeupByTimer_SetsNormalReadingMode)
{
    EXPECT_CALL(mock_system_hal, reset_reason()).WillOnce(Return(ESP_RST_DEEPSLEEP));
    EXPECT_CALL(mock_sleep, get_wakeup_cause()).WillOnce(Return(ESP_SLEEP_WAKEUP_TIMER));

    EXPECT_CALL(mock_core_storage, process_boot_reasons(_, ESP_RST_DEEPSLEEP, ESP_SLEEP_WAKEUP_TIMER, _))
        .WillOnce(testing::Invoke([](CoreData& core, esp_reset_reason_t, esp_sleep_wakeup_cause_t, bool&) {
            core.last_wake = WakeSource::TIMER;
        }));

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(sut->get_core_data().last_wake, WakeSource::TIMER);
}

TEST_F(WaterTankAppTest, Init_WakeupByGpio_SetsFloatSwitchTriggeredMode)
{
    EXPECT_CALL(mock_system_hal, reset_reason()).WillOnce(Return(ESP_RST_DEEPSLEEP));
    EXPECT_CALL(mock_sleep, get_wakeup_cause()).WillOnce(Return(ESP_SLEEP_WAKEUP_GPIO));

    EXPECT_CALL(mock_core_storage, process_boot_reasons(_, ESP_RST_DEEPSLEEP, ESP_SLEEP_WAKEUP_GPIO, _))
        .WillOnce(testing::Invoke([](CoreData& core, esp_reset_reason_t, esp_sleep_wakeup_cause_t, bool&) {
            core.last_wake = WakeSource::GPIO;
        }));

    // Act
    esp_err_t ret = sut->init(false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(sut->get_core_data().last_wake, WakeSource::GPIO);
}

TEST_F(WaterTankAppTest, Init_ResetReasonPanic_IncrementsCrashAndBootCount)
{
    EXPECT_CALL(mock_core_storage, init(_, _)).WillOnce(testing::Invoke([](CoreData& core, const CoreData&) {
        core.boot_count = 5;
        core.crash_count = 2;
        return ESP_OK;
    }));

    EXPECT_CALL(mock_system_hal, reset_reason()).WillOnce(Return(ESP_RST_PANIC));

    EXPECT_CALL(mock_core_storage, process_boot_reasons(_, ESP_RST_PANIC, _, _))
        .WillOnce(
            testing::Invoke([](CoreData& core, esp_reset_reason_t, esp_sleep_wakeup_cause_t, bool& pending_commit) {
                core.boot_count++;
                core.crash_count++;
                core.last_wake = WakeSource::CRASH;
                pending_commit = true;
            }));

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
    EXPECT_CALL(mock_tank_storage, init_app_data(_, _))
        .WillOnce(testing::Invoke([](WaterTankStats& stats, const WaterTankStats&) {
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

    // Mock initial state as not connected so connect executes
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::STARTED));

    EXPECT_CALL(mock_wifi, connect(_, _, _)).WillOnce(Return(ESP_OK));

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

    sut->run(true);

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

    sut->run(true);

    EXPECT_GT(sut->get_stats().level_permille, 0);
}

TEST_F(WaterTankAppTest, Run_PowerOnSensorFail_SetsHardwareFaultAndUnhealthySession)
{
    sut->init(false);

    EXPECT_CALL(mock_power, turn_on()).WillOnce(Return(ESP_FAIL));

    sut->run(true);

    EXPECT_EQ(sut->get_stats().last_result, ultrasonic::UsResult::HW_FAULT);
    EXPECT_FALSE(sut->is_session_healthy());
}

// --- Communication & Reporting ---

TEST_F(WaterTankAppTest, Run_ConstructsAndSendsReportViaEspNow)
{
    sut->init(false);

    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_WhenRecoveryScan_WaitsCommReadyAndRetriesReportSend)
{
    sut->init(false);

    // Expect 2 report transmissions (initial failed attempt + retry after channel recovery)
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_FAIL)).WillOnce(Return(ESP_OK));

    InSequence seq;
    // Checked at line 229 of run()
    EXPECT_CALL(mock_comm, get_node_state()).WillOnce(Return(espnow::NodeState::RECOVERY_SCAN));
    // Polled inside wait_for_comm_ready() loop
    EXPECT_CALL(mock_comm, get_node_state()).WillOnce(Return(espnow::NodeState::RECOVERY_SCAN));
    EXPECT_CALL(mock_comm, get_node_state()).WillOnce(Return(espnow::NodeState::OPERATIONAL));

    sut->run(true);
}

// --- OTA Management ---

TEST_F(WaterTankAppTest, Run_PendingFirmwareVerify_MarksPartitionValid_WhenSessionHealthy)
{
    // Arrange: Mock pending OTA verification state in run()
    EXPECT_CALL(mock_ota, check_pending_verify()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota, confirm_app_valid()).WillOnce(Return(true));
    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillRepeatedly(Return(ESP_OK));

    // Act: init and run
    esp_err_t ret = sut->init(false);
    EXPECT_EQ(ret, ESP_OK);

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_RollsBackFirmware_WhenSessionNotHealthy)
{
    // Arrange: Mock pending_verify state and healthy = false
    EXPECT_CALL(mock_ota, check_pending_verify()).WillOnce(Return(true));

    // Make session unhealthy by failing WiFi initialization (or another component after wifi is connected/started)
    // To test disconnect_stop_wifi, wifi get_state must be != UNINITIALIZED and != INITIALIZED
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::CONNECTED_GOT_IP));
    EXPECT_CALL(mock_wifi, init(_)).WillOnce(Return(ESP_FAIL));

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

    EXPECT_CALL(mock_comm, deinit()).Times(2);
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::CONNECTED_GOT_IP));
    EXPECT_CALL(mock_ota, start_ota()).Times(1);
    EXPECT_CALL(mock_ota, get_status()).WillRepeatedly(Return(OtaStatus::READY_TO_RESTART));

    sut->run(true);
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
    EXPECT_CALL(mock_sleep, deep_sleep_start()).Times(1);

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_RestartsSystem_WhenNoWakeupConfigured)
{
    sut->init(false);

    EXPECT_CALL(mock_float_switch, should_enable_wakeup()).WillOnce(Return(false));

    EXPECT_CALL(mock_sleep, disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_sleep, enable_timer_wakeup(_)).WillOnce(Return(ESP_FAIL));

    EXPECT_CALL(mock_system_hal, restart()).Times(1);
    EXPECT_CALL(mock_sleep, deep_sleep_start()).Times(0);

    sut->run(true);
}

// --- Battery, Report & Persistence ---

TEST_F(WaterTankAppTest, Run_ReadsBattery_UpdatesStatsAndDeinits)
{
    sut->init(false);

    battery_monitor::BatteryReading bat_reading{};
    bat_reading.voltage_mv = 3800;
    bat_reading.percent = 85;
    bat_reading.state = battery_monitor::BatteryState::NORMAL;

    EXPECT_CALL(mock_battery, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_battery, read(_)).WillOnce(DoAll(SetArgReferee<0>(bat_reading), Return(ESP_OK)));
    EXPECT_CALL(mock_battery, deinit()).WillOnce(Return(ESP_OK));

    sut->run(true);

    EXPECT_EQ(sut->get_stats().last_battery_mv, 3800);
    EXPECT_EQ(sut->get_stats().last_battery_percent, 85);
    EXPECT_EQ(sut->get_stats().last_battery_state, farm::BatteryState::NORMAL);
}

TEST_F(WaterTankAppTest, Run_SendsFloatSwitchFullFlagInReport)
{
    sut->init(false);

    EXPECT_CALL(mock_float_switch, is_tank_full()).WillOnce(Return(true));

    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _))
        .WillOnce(Invoke([](uint8_t dest, uint8_t type, const void* data, size_t len, bool ack) {
            const auto* report = static_cast<const farm::WaterLevelReport*>(data);
            EXPECT_TRUE(report->float_switch_is_full);
            return ESP_OK;
        }));

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_ProcessesStartOtaCommand)
{
    EXPECT_CALL(mock_command_handler, process(_))
        .WillOnce(Return(TankCommandProcessResult{.ota_requested = true}));

    EXPECT_CALL(mock_comm, deinit()).Times(2);
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::CONNECTED_GOT_IP));
    EXPECT_CALL(mock_ota, start_ota()).Times(1);
    EXPECT_CALL(mock_ota, get_status()).WillRepeatedly(Return(OtaStatus::READY_TO_RESTART));

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_ProcessesRebootCommand)
{
    EXPECT_CALL(mock_command_handler, process(_))
        .WillOnce(Return(TankCommandProcessResult{.reboot_requested = true}));

    EXPECT_CALL(mock_core_storage, save_core(_, false)).Times(1);
    EXPECT_CALL(mock_tank_storage, save_app_data(_, false)).Times(1);
    EXPECT_CALL(mock_system_hal, restart()).Times(1);

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_ProcessesSleepOverrideCommand)
{
    EXPECT_CALL(mock_command_handler, process(_))
        .WillOnce(Return(TankCommandProcessResult{.override_sleep_us = 120000000ULL}));

    EXPECT_CALL(mock_sleep, enable_timer_wakeup(120000000ULL)).WillOnce(Return(ESP_OK));

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_ProcessesSleepOverride_ConfiguresTimerWakeup)
{
    EXPECT_CALL(mock_command_handler, process(_))
        .WillOnce(Return(TankCommandProcessResult{.override_sleep_us = 60000000ULL}));

    EXPECT_CALL(mock_sleep, enable_timer_wakeup(60000000ULL)).WillOnce(Return(ESP_OK));

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_PeriodicNvsCommit_ForcesCommitWhenIntervalReached)
{
    EXPECT_CALL(mock_tank_storage, init_app_data(_, _))
        .WillOnce(Invoke([](WaterTankStats& stats, const WaterTankStats&) {
            stats.cycles_since_nvs_commit = 10;
            return ESP_OK;
        }));

    sut->init(false);

    EXPECT_CALL(mock_core_storage, save_core(_, true)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_tank_storage, save_app_data(_, true)).WillOnce(Return(ESP_OK));

    sut->run(true);

    EXPECT_EQ(sut->get_stats().cycles_since_nvs_commit, 0);
}

TEST_F(WaterTankAppTest, Run_CancelsOta_WhenOtaFails)
{
    sut->init(false);
    sut->on_ota_triggered(OtaTriggerSource::ESPNOW);

    EXPECT_CALL(mock_comm, deinit()).Times(2);
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::CONNECTED_GOT_IP));
    EXPECT_CALL(mock_ota, start_ota()).Times(1);

    EXPECT_CALL(mock_ota, get_status()).WillRepeatedly(Return(OtaStatus::FAILED));
    EXPECT_CALL(mock_ota, get_last_error()).WillOnce(Return(OtaFailReason::MANIFEST_HTTP_FAIL));
    EXPECT_CALL(mock_ota, cancel_ota()).Times(1);

    sut->run(true);
}

// --- Protected Methods Direct Tests ---

TEST_F(WaterTankAppTest, WaitForCommReady_ReturnsFalseIfNotRecoveryOrOperational)
{
    EXPECT_CALL(mock_comm, get_node_state()).WillOnce(Return(espnow::NodeState::UNINITIALIZED));
    EXPECT_FALSE(sut->call_wait_for_comm_ready(100));
}

TEST_F(WaterTankAppTest, WaitForCommReady_WaitsAndReturnsTrueIfRecovers)
{
    EXPECT_CALL(mock_comm, get_node_state())
        .WillOnce(Return(espnow::NodeState::RECOVERY_SCAN))
        .WillOnce(Return(espnow::NodeState::OPERATIONAL));

    EXPECT_CALL(mock_rtos, task_delay(pdMS_TO_TICKS(100))).Times(1);

    EXPECT_TRUE(sut->call_wait_for_comm_ready(1000));
}

TEST_F(WaterTankAppTest, WaitForCommReady_WaitsAndReturnsFalseIfTimeout)
{
    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::RECOVERY_SCAN));

    EXPECT_CALL(mock_rtos, task_delay(pdMS_TO_TICKS(100))).Times(testing::AtLeast(1));

    EXPECT_FALSE(sut->call_wait_for_comm_ready(500));
}

TEST_F(WaterTankAppTest, ProcessPendingOta_ConnectsWifiAndRollbacksOnFail)
{
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::INITIALIZED));
    EXPECT_CALL(mock_wifi, connect(_, _, _)).WillRepeatedly(Return(ESP_FAIL));

    EXPECT_CALL(mock_comm, deinit()).Times(1);

    EXPECT_CALL(mock_comm, init(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_comm, set_channel_policy(espnow::ChannelPolicy::SCAN)).Times(1);
    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));

    sut->call_process_pending_ota();
}

TEST_F(WaterTankAppTest, ProcessPendingOta_FailsOnWatchdogTimeoutAndLogs)
{
    EXPECT_CALL(mock_wifi, get_state()).WillRepeatedly(Return(wifi_manager::State::CONNECTED_GOT_IP));

    EXPECT_CALL(mock_comm, deinit()).Times(1);
    EXPECT_CALL(mock_ota, start_ota()).Times(1);

    EXPECT_CALL(mock_ota, get_status()).WillRepeatedly(Return(OtaStatus::DOWNLOADING));

    EXPECT_CALL(mock_rtos, task_delay(pdMS_TO_TICKS(500))).Times(testing::AtLeast(1));

    EXPECT_CALL(mock_ota, cancel_ota()).Times(1);

    EXPECT_CALL(mock_comm, init(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_comm, set_channel_policy(espnow::ChannelPolicy::FIXED)).Times(1);
    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));

    sut->call_process_pending_ota();
}

TEST_F(WaterTankAppTest, CheckFirmwareHealthy_ConfirmsAppAndSendsReportOnSuccess)
{
    sut->set_session_healthy(true);

    EXPECT_CALL(mock_ota, confirm_app_valid()).WillOnce(Return(true));

    EXPECT_CALL(mock_comm, get_node_state()).WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _)).WillOnce(Return(ESP_OK));

    sut->call_check_firmware_healthy();
}

TEST_F(WaterTankAppTest, UpdateRunningVersion_PopulatesCoreVersion)
{
    OtaVersion mock_desc{1, 2, 3};
    EXPECT_CALL(mock_ota, get_running_version()).WillRepeatedly(Return(mock_desc));

    sut->init(false);

    EXPECT_TRUE(sut->is_pending_core_commit());

    const CoreData& core = sut->get_core_data();
    EXPECT_EQ(core.fw_major, 1);
    EXPECT_EQ(core.fw_minor, 2);
    EXPECT_EQ(core.fw_patch, 3);
}

TEST_F(WaterTankAppTest, Run_ProcessesSyncTimeCommand_PopulatesCore)
{
    EXPECT_CALL(mock_command_handler, process(_))
        .WillOnce(Return(TankCommandProcessResult{.time_synced = true}));

    EXPECT_CALL(mock_time_manager, is_synchronized()).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_time_manager, get_timestamp_ms()).WillRepeatedly(Return(1700000000000ULL));

    EXPECT_CALL(mock_core_storage, save_core(_, true)).WillOnce(Return(ESP_OK));

    sut->run(true);

    EXPECT_TRUE(sut->get_core_data().has_valid_time);
    EXPECT_EQ(1700000000000ULL, sut->get_core_data().last_sync_unix_time_ms);
}

TEST_F(WaterTankAppTest, Run_UnsynchronizedTime_DoesNotSendActiveTimeSyncRequest)
{
    EXPECT_CALL(Const(mock_time_manager), is_synchronized()).WillRepeatedly(Return(false));

    bool time_sync_requested = false;
    EXPECT_CALL(mock_comm, send_data(_, _, _, _, _))
        .WillRepeatedly(
            Invoke([&time_sync_requested](uint8_t dest, uint8_t type, const void* data, size_t len, bool ack) {
                if (type == static_cast<uint8_t>(farm::PayloadType::REQUEST_TIME_SYNC)) {
                    time_sync_requested = true;
                }
                return ESP_OK;
            }));

    sut->run(true);

    EXPECT_FALSE(time_sync_requested);
}

TEST_F(WaterTankAppTest, Run_ProcessesMultipleCommandsInListenWindow)
{
    EXPECT_CALL(mock_command_handler, process(_))
        .WillOnce(Return(TankCommandProcessResult{
            .time_synced = true,
            .override_sleep_us = 300000000ULL}));

    EXPECT_CALL(mock_time_manager, is_synchronized()).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_time_manager, get_timestamp_ms()).WillRepeatedly(Return(1710000000000ULL));

    EXPECT_CALL(mock_sleep, enable_timer_wakeup(300000000ULL)).WillOnce(Return(ESP_OK));

    sut->run(true);

    EXPECT_TRUE(sut->get_core_data().has_valid_time);
    EXPECT_EQ(1710000000000ULL, sut->get_core_data().last_sync_unix_time_ms);
}

TEST_F(WaterTankAppTest, Run_PairingNodeState_AddsHubPeer)
{
    EXPECT_CALL(mock_comm, get_node_state())
        .WillOnce(Return(espnow::NodeState::PAIRING))
        .WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));

    EXPECT_CALL(mock_comm, add_peer(espnow::ReservedIds::HUB, _, espnow::ReservedTypes::HUB, 0))
        .WillOnce(Return(ESP_OK));

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_HighVarianceSensorResult_RetriesWith1_8xSamples)
{
    ultrasonic::Reading high_var_reading;
    high_var_reading.result = ultrasonic::UsResult::HIGH_VARIANCE;
    high_var_reading.cm = 0.0f;

    ultrasonic::Reading ok_reading;
    ok_reading.result = ultrasonic::UsResult::OK;
    ok_reading.cm = 75.0f;

    InSequence seq;
    // Initial read (default 11 samples)
    EXPECT_CALL(mock_sensor, read_level(11)).WillOnce(Return(high_var_reading));
    // Retry with 1.8x sample count (19 samples: 11 * 1.8 = 19)
    EXPECT_CALL(mock_sensor, read_level(19)).WillOnce(Return(ok_reading));

    sut->run(true);

    EXPECT_GT(sut->get_stats().level_permille, 0);
}

// ==============================================================================
// LED Status Indicator Tests
// ==============================================================================

TEST_F(WaterTankAppTest, Init_Success_SetsBootSuccessPattern)
{
    EXPECT_CALL(mock_led_controller, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_led_controller, start()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_led_controller, set_pattern(BlinkPattern::BOOT_SUCCESS)).Times(1);

    EXPECT_EQ(sut->init(false), ESP_OK);
}

TEST_F(WaterTankAppTest, Init_Failure_SetsErrorBurstPattern)
{
    ON_CALL(mock_core_storage, init(_, _)).WillByDefault(Return(ESP_FAIL));

    EXPECT_CALL(mock_led_controller, set_pattern(BlinkPattern::ERROR_BURST)).Times(1);

    EXPECT_EQ(sut->init(false), ESP_FAIL);
}

TEST_F(WaterTankAppTest, Run_SendsReport_PulsesLed)
{
    sut->init(false);

    EXPECT_CALL(mock_led_controller, set_pattern(BlinkPattern::TX_PULSE)).Times(::testing::AtLeast(1));

    sut->run(true);
}

TEST_F(WaterTankAppTest, Run_EntersDeepSleep_StopsLed)
{
    sut->init(false);

    EXPECT_CALL(mock_led_controller, stop()).Times(1);

    sut->run(true);
}

TEST_F(WaterTankAppTest, OnOtaTriggered_SetsOtaUpdatingPattern)
{
    EXPECT_CALL(mock_led_controller, set_pattern(BlinkPattern::OTA_UPDATING)).Times(1);

    sut->on_ota_triggered(OtaTriggerSource::BUTTON);
}

TEST_F(WaterTankAppTest, Run_PairingNodeState_SetsPairingPatternAndClearsWhenOperational)
{
    EXPECT_CALL(mock_comm, get_node_state())
        .WillOnce(Return(espnow::NodeState::PAIRING))
        .WillRepeatedly(Return(espnow::NodeState::OPERATIONAL));

    EXPECT_CALL(mock_comm, add_peer(espnow::ReservedIds::HUB, _, espnow::ReservedTypes::HUB, 0))
        .WillOnce(Return(ESP_OK));

    EXPECT_CALL(mock_led_controller, set_pattern(BlinkPattern::PAIRING_MODE)).Times(1);
    EXPECT_CALL(mock_led_controller, set_pattern(BlinkPattern::OFF)).Times(1);
    EXPECT_CALL(mock_led_controller, set_pattern(BlinkPattern::TX_PULSE)).Times(::testing::AtLeast(1));

    sut->run(true);
}
