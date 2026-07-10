#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "water_tank_logic.hpp"
#include "mock_i_float_switch.hpp"

using ::testing::Return;

class WaterTankLogicTest : public ::testing::Test
{
protected:
    TankGeometry geometry{20}; // 20cm offset
    floatswitch::MockFloatSwitch mock_fs;
    WaterTankLogic logic{geometry, mock_fs};
    WaterTankStats stats;

    void SetUp() override { stats.reset(); }
};

TEST_F(WaterTankLogicTest, ProcessOkReadingUpdatesStats)
{
    ultrasonic::Reading reading = {ultrasonic::UsResult::OK, 50.0f}; // 30cm depth

    logic.process_reading(reading, stats);

    EXPECT_EQ(stats.last_result, ultrasonic::UsResult::OK);
    EXPECT_EQ(stats.measure_count, 1);
    EXPECT_EQ(stats.ok_count, 1);
    EXPECT_EQ(stats.level_permille, 751);           // 30cm depth in 150cm tank
    EXPECT_EQ(stats.fill_state, FillState::STABLE); // First reading
}

TEST_F(WaterTankLogicTest, DetectsFillingState)
{
    // First reading
    logic.process_reading({ultrasonic::UsResult::OK, 100.0f}, stats);
    uint16_t first_level = stats.level_permille;

    // Second reading: closer (fuller)
    logic.process_reading({ultrasonic::UsResult::OK, 50.0f}, stats);

    EXPECT_GT(stats.level_permille, first_level);
    EXPECT_EQ(stats.fill_state, FillState::FILLING);
}

TEST_F(WaterTankLogicTest, DetectsDrainingState)
{
    // First reading
    logic.process_reading({ultrasonic::UsResult::OK, 50.0f}, stats);
    uint16_t first_level = stats.level_permille;

    // Second reading: further (emptier)
    logic.process_reading({ultrasonic::UsResult::OK, 100.0f}, stats);

    EXPECT_LT(stats.level_permille, first_level);
    EXPECT_EQ(stats.fill_state, FillState::DRAINING);
}

TEST_F(WaterTankLogicTest, EntersBackupModeOnConsecutiveFailures)
{
    // Initial state
    stats.consecutive_failures = 0;
    stats.backup_mode_active = false;

    // Failures
    for (int i = 0; i < CONSECUTIVE_FAILURES_THRESHOLD; ++i) {
        stats.last_result = ultrasonic::UsResult::TIMEOUT;
        logic.update_operation_mode(stats);
    }

    EXPECT_EQ(stats.consecutive_failures, CONSECUTIVE_FAILURES_THRESHOLD);
    EXPECT_TRUE(stats.backup_mode_active);
}

TEST_F(WaterTankLogicTest, SleepTimeInBackupModeDependsOnFloatSwitch)
{
    stats.backup_mode_active = true;

    // Tank NOT full according to float switch
    EXPECT_CALL(mock_fs, is_tank_full()).WillOnce(Return(false));
    EXPECT_EQ(logic.calculate_sleep_time_us(stats), BACKUP_MODE_SLEEP_US); // 15s (BACKUP_MODE_SLEEP_US)

    // Tank IS full
    EXPECT_CALL(mock_fs, is_tank_full()).WillOnce(Return(true));
    EXPECT_EQ(logic.calculate_sleep_time_us(stats), TIMER_STABLE_US); // 5min (TIMER_STABLE_US)
}

TEST_F(WaterTankLogicTest, ProcessBatteryCalculatesPercentAndState)
{
    // 1. Initial UNKNOWN state, normal voltage (3600 mV -> 50%)
    logic.process_battery(3600, stats);
    EXPECT_EQ(stats.last_battery_mv, 3600);
    EXPECT_EQ(stats.last_battery_percent, 50);
    EXPECT_EQ(stats.last_battery_state, BatteryState::NORMAL);

    // 2. Linear capping (above full)
    logic.process_battery(4300, stats);
    EXPECT_EQ(stats.last_battery_percent, 100);
    EXPECT_EQ(stats.last_battery_state, BatteryState::FULL);

    // 3. Linear capping (below empty)
    logic.process_battery(2900, stats);
    EXPECT_EQ(stats.last_battery_percent, 0);
    EXPECT_EQ(stats.last_battery_state, BatteryState::CRITICAL);
}

TEST_F(WaterTankLogicTest, ProcessBatteryAppliesStateHysteresis)
{
    // Start with NORMAL
    logic.process_battery(3600, stats);
    ASSERT_EQ(stats.last_battery_state, BatteryState::NORMAL);

    // Drop to exactly LOW threshold (3400 mV) -> State becomes LOW
    logic.process_battery(3400, stats);
    EXPECT_EQ(stats.last_battery_state, BatteryState::LOW);

    // Slight recovery to 3430 mV (less than 3400 + 50 mV hysteresis) -> State should remain LOW
    logic.process_battery(3430, stats);
    EXPECT_EQ(stats.last_battery_state, BatteryState::LOW);

    // Recover past hysteresis threshold (3460 mV) -> State becomes NORMAL
    logic.process_battery(3460, stats);
    EXPECT_EQ(stats.last_battery_state, BatteryState::NORMAL);

    // Drop to exactly CRITICAL threshold (3200 mV) -> State becomes CRITICAL
    logic.process_battery(3200, stats);
    EXPECT_EQ(stats.last_battery_state, BatteryState::CRITICAL);

    // Slight recovery to 3230 mV -> State should remain CRITICAL
    logic.process_battery(3230, stats);
    EXPECT_EQ(stats.last_battery_state, BatteryState::CRITICAL);

    // Recovery to 3260 mV (past 3200 + 50 mV hysteresis) -> State becomes LOW
    logic.process_battery(3260, stats);
    EXPECT_EQ(stats.last_battery_state, BatteryState::LOW);
}

