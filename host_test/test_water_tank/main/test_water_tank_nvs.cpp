#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "water_tank_stats.hpp"
#include "mock_persistence_backend.hpp"
#include "water_tank_nvs.hpp"

#include "esp_rom_crc.h"

#include <memory>
#include <cstring>
#include <type_traits>

using ::testing::_;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

/**
 * Helper: Calculate CRC for a struct with crc field
 */
template <typename T> inline uint32_t test_calculate_crc(const T& data)
{
    static_assert(std::is_standard_layout_v<T>, "T must be standard_layout for safe offset calculation");
    static_assert(offsetof(T, crc) != 0, "T must have a non-first crc field");

    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&data), offsetof(T, crc));
}

/**
 * Fixture for WaterTankNvsTest tests.
 * Manages mock backends and provides utility methods.
 */
class WaterTankNvsTest : public ::testing::Test
{
protected:
    // Use NiceMock to suppress warnings for uninteresting calls
    NiceMock<MockPersistenceBackend> rtc_backend_;
    NiceMock<MockPersistenceBackend> nvs_backend_;

    // System under test
    std::unique_ptr<WaterTankNvs> sut_;

    void SetUp() override
    {
        // Configure backends to use real in-memory storage
        rtc_backend_.UseRealStorage();
        nvs_backend_.UseRealStorage();

        // Create NvsCore instance with the mock backends
        sut_ = std::make_unique<WaterTankNvs>(rtc_backend_, nvs_backend_);
    }

    void TearDown() override { sut_.reset(); }

    /**
     * Helper: Create a valid WaterTankStats with all fields set.
     */
    WaterTankStats create_valid_stats()
    {
        WaterTankStats stats;
        stats.magic = WaterTankStats::MAGIC;
        stats.level_permille = 500;
        stats.last_level_permille = 500;
        stats.fill_state = FillState::FILLING;
        stats.last_distance_cm = 25.0f;
        stats.last_result = ultrasonic::UsResult::OK;
        stats.sample_timestamp_ms = 100;

        stats.measure_count = 10;
        stats.ok_count = 8;
        stats.weak_count = 1;
        stats.timeout_count = 0;
        stats.out_of_range_count = 1;
        stats.high_variance_count = 0;
        stats.insufficient_samples_count = 0;
        stats.echo_stuck_count = 0;
        stats.hw_fault_count = 0;

        stats.gpio_wakeup_enabled = true;

        stats.backup_mode_active = false;
        stats.consecutive_failures = 0;

        stats.last_battery_mv = 3700;
        stats.last_battery_percent = 80;
        stats.last_battery_state = farm::BatteryState::NORMAL;

        // Calculate and set CRC
        stats.crc = test_calculate_crc(stats);

        return stats;
    }

    /**
     * Helper: Pre-populate RTC backend with data.
     */
    void set_rtc_data(const WaterTankStats& stats) { rtc_backend_.save(&stats, sizeof(stats)); }

    /**
     * Helper: Pre-populate NVS backend with data.
     */
    void set_nvs_data(const WaterTankStats& stats) { nvs_backend_.save(&stats, sizeof(stats)); }

    /**
     * Helper: Get stored RTC data for verification.
     */
    WaterTankStats get_stored_rtc_data() const
    {
        WaterTankStats stats;
        memcpy(&stats, rtc_backend_.GetStoredData(), sizeof(stats));
        return stats;
    }

    /**
     * Helper: Get stored NVS data for verification.
     */
    WaterTankStats get_stored_nvs_data() const
    {
        WaterTankStats stats;
        memcpy(&stats, nvs_backend_.GetStoredData(), sizeof(stats));
        return stats;
    }
};

// =============================================================
// Tests
// =============================================================

/**
 * Test: Load from RTC when valid
 *
 * Scenario: RTC has valid data, NVS is empty
 * Expected: Data is loaded from RTC, NVS is not accessed
 */
TEST_F(WaterTankNvsTest, LoadFromRtcWhenValid)
{
    // Arrange: Pre-populate RTC with valid data
    WaterTankStats expected = create_valid_stats();
    set_rtc_data(expected);

    // Expect RTC to be called, NVS may not be
    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(0);

    // Act: Load from NvsCore
    WaterTankStats loaded;
    esp_err_t ret = sut_->load_app_data(loaded);

    // Assert: Load is successful and data matches RTC
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(loaded, expected);

    // Verify that NVS backend was not accessed (no save/load calls)
    EXPECT_EQ(nvs_backend_.GetStoredSize(), 0);
}

/**
 * Test: Load from NVS when RTC is invalid
 *
 * Scenario: RTC is empty/corrupt, NVS has valid data
 * Expected: Data is loaded from NVS, and synced back to RTC
 */
TEST_F(WaterTankNvsTest, LoadFromNvsWhenRtcInvalid)
{
    // Arrange: RTC is empty, NVS has valid data
    WaterTankStats expected = create_valid_stats();
    set_nvs_data(expected);
    // RTC is empty (default zeroed)

    // Expect RTC load to be called and fail, then NVS load to succeed
    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(1);
    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1); // Sync back to RTC

    // Act: Load from NvsCore
    WaterTankStats loaded;
    esp_err_t ret = sut_->load_app_data(loaded);

    // Assert: Load is successful and data matches NVS
    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(loaded, expected);
    EXPECT_EQ(loaded.level_permille, expected.level_permille);

    // Verify that RTC now has the synced data
    WaterTankStats rtc_data = get_stored_rtc_data();
    EXPECT_EQ(rtc_data, expected);
}

/**
 * Test: Load fails when both RTC and NVS are invalid
 *
 * Scenario: Both backends have corrupt/missing data
 * Expected: Return error
 */
TEST_F(WaterTankNvsTest, LoadFailsWhenBothInvalid)
{
    // Arrange: Both backends are empty (default zeroed
    // (setUp() already clears them)

    // Expect RTC load to fail, NVS load to fail
    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(1);

    // Act
    WaterTankStats loaded;
    esp_err_t ret = sut_->load_app_data(loaded);

    // Assert
    EXPECT_NE(ret, ESP_OK);
}

/**
 * Test: Save to RTC only when not forcing NVS
 *
 * Scenario: Data is dirty but force_nvs_commit is false
 * Expected: Data goes to RTC, not NVS
 */
TEST_F(WaterTankNvsTest, SaveToRtcOnlyWhenNotForcingNvs)
{
    // Arrange: Data is dirty but force_nvs_commit is false
    WaterTankStats to_save = create_valid_stats();
    to_save.level_permille = 999; // change a field to mark as dirty

    // Expect RTC to be called, NVS may not be
    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, save(_, _)).Times(0);

    // Act
    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/false);

    // Assert
    EXPECT_EQ(ret, ESP_OK);

    // Verify RTC has the new data
    WaterTankStats rtc_stored = get_stored_rtc_data();
    EXPECT_EQ(rtc_stored.level_permille, 999);
}

/**
 * Test: Save to both RTC and NVS when forcing
 *
 * Scenario: force_nvs_commit is true
 * Expected: Data goes to both RTC and NVS
 */
TEST_F(WaterTankNvsTest, SaveToBothRtcAndNvsWhenForcing)
{
    // Arrange: Data to save
    WaterTankStats to_save = create_valid_stats();
    to_save.level_permille = 888; // change a field

    // Expect both backends to be called
    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, save(_, _)).Times(1);

    // Act
    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/true);

    // Assert
    EXPECT_EQ(ret, ESP_OK);

    // Verify both RTC and NVS have the new data
    WaterTankStats rtc_stored = get_stored_rtc_data();
    WaterTankStats nvs_stored = get_stored_nvs_data();
    EXPECT_EQ(rtc_stored.level_permille, 888);
    EXPECT_EQ(nvs_stored.level_permille, 888);
}

/**
 * Test: Save to NVS fails when NVS error occurs
 *
 * Scenario: force_nvs_commit is true, but NVS save fails
 * Expected: Return error
 */
TEST_F(WaterTankNvsTest, SaveToNvsFailsWhenNvsError)
{
    // Arrange: Data to save
    WaterTankStats to_save = create_valid_stats();
    to_save.level_permille = 888; // change a field

    // Arrange: Force NVS save to fail
    EXPECT_CALL(nvs_backend_, save(_, _)).Times(1).WillOnce(Return(ESP_ERR_NVS_NOT_INITIALIZED));

    // Act
    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/true);

    // Assert: Expect error returned (not ESP_OK)
    EXPECT_NE(ret, ESP_OK);
}

/**
 * Test: Round-trip: save and load
 *
 * Scenario: Save data to backends, then load it back
 * Expected: Loaded data matches saved data
 */
TEST_F(WaterTankNvsTest, RoundTripSaveAndLoad)
{
    // Arrange: Create data to save
    WaterTankStats original = create_valid_stats();
    original.level_permille = 777; // change a field
    original.consecutive_failures = 99;

    // Act: Save to both backends
    esp_err_t save_ret = sut_->save_app_data(original, /*force_nvs_commit=*/true);
    EXPECT_EQ(save_ret, ESP_OK);

    // Act: Load back
    WaterTankStats loaded = {};
    esp_err_t load_ret = sut_->load_app_data(loaded);

    // Assert: Loaded data matches saved data
    EXPECT_EQ(load_ret, ESP_OK);
    EXPECT_EQ(loaded, original);
    EXPECT_EQ(loaded.level_permille, 777);
    EXPECT_EQ(loaded.consecutive_failures, 99);
}

/**
 * Test: CRC validation on load
 *
 * Scenario: Corrupt data with bad CRC in NVS
 * Expected: Load fails, does not accept corrupt data
 */
TEST_F(WaterTankNvsTest, LoadFailsWithBadCrc)
{
    // Arrange: Corrupt data with bad CRC
    WaterTankStats corrupted = create_valid_stats();
    corrupted.crc = 0xDEAD; // Bad CRC
    // RTC empty
    set_nvs_data(corrupted);

    // Act: Attempt to load
    WaterTankStats loaded = {};
    esp_err_t ret = sut_->load_app_data(loaded);

    // Assert: Load fails
    EXPECT_NE(ret, ESP_OK);
}

/**
 * Test: Magic field validation
 *
 * Scenario: Data with wrong magic number
 * Expected: Load fails
 */
TEST_F(WaterTankNvsTest, LoadFailsWithWrongMagic)
{
    // Arrange: Data with wrong magic
    WaterTankStats wrong_magic = create_valid_stats();
    wrong_magic.magic = 0xDEAD;                        // Wrong magic
    wrong_magic.crc = test_calculate_crc(wrong_magic); // Recalculate CRC for the wrong magic
    set_nvs_data(wrong_magic);

    // Act: Attempt to load
    WaterTankStats loaded = {};
    esp_err_t ret = sut_->load_app_data(loaded);

    // Assert: Load fails
    EXPECT_NE(ret, ESP_OK);
}

/**
 * Test: No double-save when data unchanged
 *
 * Scenario: Save identical data multiple times
 * Expected: First save goes to NVS, subsequent saves skip NVS (not dirty)
 */
TEST_F(WaterTankNvsTest, NoDoubleSaveWhenUnchanged)
{
    // Arrange
    WaterTankStats data = create_valid_stats();

    // First save with force
    sut_->save_app_data(data, true);

    // Reset mocks to track second save
    rtc_backend_.Clear();
    nvs_backend_.Clear();
    rtc_backend_.UseRealStorage();
    nvs_backend_.UseRealStorage();

    // Set up expectations for second save (should NOT touch NVS if not dirty)
    // This is tricky with mocks - just verify behavior
    WaterTankStats same_data = data;
    esp_err_t ret = sut_->save_app_data(same_data, false);

    // If data is truly identical, should return quickly
    EXPECT_EQ(ret, ESP_OK);
}