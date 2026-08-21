#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "water_tank_logic.hpp"
#include "mock_float_switch.hpp"

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
    EXPECT_EQ(stats.level_permille, 777);           // 30cm depth in tank with 5-cylinder LUT (between 22cm/835ppm and 52cm/616ppm)
    EXPECT_EQ(stats.fill_state, FillState::STABLE); // First reading
}

TEST_F(WaterTankLogicTest, DetectsFillingState)
{
    // First reading (anchors baseline level_permille = 100cm distance)
    logic.process_reading({ultrasonic::UsResult::OK, 100.0f}, stats);
    uint16_t first_level = stats.level_permille;

    // Second reading: 70cm distance (fuller) -> raw direction FILLING, pending count = 1
    logic.process_reading({ultrasonic::UsResult::OK, 70.0f}, stats);
    EXPECT_EQ(stats.fill_state, FillState::STABLE); // Not confirmed yet

    // Third reading: 40cm distance (even fuller) -> raw direction FILLING, pending count = 2 -> TRANSITION!
    logic.process_reading({ultrasonic::UsResult::OK, 40.0f}, stats);

    EXPECT_GT(stats.level_permille, first_level);
    EXPECT_EQ(stats.fill_state, FillState::FILLING);
}

TEST_F(WaterTankLogicTest, DetectsDrainingState)
{
    // First reading (anchors baseline level_permille = 40cm distance)
    logic.process_reading({ultrasonic::UsResult::OK, 40.0f}, stats);
    uint16_t first_level = stats.level_permille;

    // Second reading: 70cm distance (emptier) -> raw direction DRAINING, pending count = 1
    logic.process_reading({ultrasonic::UsResult::OK, 70.0f}, stats);
    EXPECT_EQ(stats.fill_state, FillState::STABLE); // Not confirmed yet

    // Third reading: 100cm distance (even emptier) -> raw direction DRAINING, pending count = 2 -> TRANSITION!
    logic.process_reading({ultrasonic::UsResult::OK, 100.0f}, stats);

    EXPECT_LT(stats.level_permille, first_level);
    EXPECT_EQ(stats.fill_state, FillState::DRAINING);
}

TEST_F(WaterTankLogicTest, RejectsSingleSampleNoiseInFillingState)
{
    // Baseline
    logic.process_reading({ultrasonic::UsResult::OK, 100.0f}, stats);
    // Transition to FILLING with 2 consecutive filling samples
    logic.process_reading({ultrasonic::UsResult::OK, 80.0f}, stats);
    logic.process_reading({ultrasonic::UsResult::OK, 60.0f}, stats);
    ASSERT_EQ(stats.fill_state, FillState::FILLING);

    // Single noisy sample (reading drops slightly due to wave turbulence -> raw STABLE)
    logic.process_reading({ultrasonic::UsResult::OK, 60.0f}, stats);
    EXPECT_EQ(stats.fill_state, FillState::FILLING); // State remains FILLING!

    // Next sample continues filling -> pending noise is discarded, state remains FILLING
    logic.process_reading({ultrasonic::UsResult::OK, 40.0f}, stats);
    EXPECT_EQ(stats.fill_state, FillState::FILLING);
    EXPECT_EQ(stats.pending_state_count, 0);
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

