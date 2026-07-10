// components/battery_monitor/host_test/test_battery_monitor/main/test_battery_monitor.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

// Mocks
#include "mock_i_adc_battery_reader.hpp"
#include "mock_hal_adc_oneshot.hpp"
#include "mock_hal_adc_calibration.hpp"
#include "mock_bm_hal_timer.hpp"

// Classes under test
#include "battery_monitor.hpp"
#include "adc_battery_reader.hpp"
#include "battery_monitor_types.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::DoAll;
using ::testing::SetArgPointee;
using ::testing::SetArgReferee;
using ::testing::Exactly;

namespace battery_monitor {
namespace test {

// =============================================================================
// BatteryMonitor Tests
// =============================================================================

class BatteryMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_reader = std::make_shared<NiceMock<MockAdcBatteryReader>>();
        config_ = {}; // Default: divider 240k/240k, range 3.0V - 4.2V
        monitor = std::make_unique<BatteryMonitor>(*mock_reader, config_);
    }

    std::shared_ptr<NiceMock<MockAdcBatteryReader>> mock_reader;
    BatteryMonitorConfig config_;
    std::unique_ptr<BatteryMonitor> monitor;
};

TEST_F(BatteryMonitorTest, InitSuccess) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    EXPECT_EQ(monitor->init(), ESP_OK);
    EXPECT_TRUE(monitor->is_initialized());
}

TEST_F(BatteryMonitorTest, InitFailure) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_FAIL));
    EXPECT_EQ(monitor->init(), ESP_FAIL);
    EXPECT_FALSE(monitor->is_initialized());
}

TEST_F(BatteryMonitorTest, DoubleInitReturnsInvalidState) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    EXPECT_EQ(monitor->init(), ESP_OK);
    EXPECT_EQ(monitor->init(), ESP_ERR_INVALID_STATE);
}

TEST_F(BatteryMonitorTest, DeinitSuccess) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    EXPECT_EQ(monitor->init(), ESP_OK);

    EXPECT_CALL(*mock_reader, deinit()).WillOnce(Return(ESP_OK));
    EXPECT_EQ(monitor->deinit(), ESP_OK);
    EXPECT_FALSE(monitor->is_initialized());
}

TEST_F(BatteryMonitorTest, DeinitNotInitializedReturnsInvalidState) {
    EXPECT_EQ(monitor->deinit(), ESP_ERR_INVALID_STATE);
}

TEST_F(BatteryMonitorTest, ReadUninitializedReturnsInvalidState) {
    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_ERR_INVALID_STATE);
}

TEST_F(BatteryMonitorTest, ReadSuccessNormalState) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    ASSERT_EQ(monitor->init(), ESP_OK);

    // 2000 mV on ADC should map to 4000 mV on Battery (divider 240k/240k -> 2.0x ratio)
    EXPECT_CALL(*mock_reader, read_adc_mv(_))
        .WillOnce(DoAll(SetArgReferee<0>(2000), Return(ESP_OK)));

    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_OK);
    EXPECT_EQ(reading.adc_mv, 2000);
    EXPECT_EQ(reading.voltage_mv, 4000);
    EXPECT_EQ(reading.percent, 83); // (4000-3000)*100/(4200-3000) = 83.3%
    EXPECT_EQ(reading.state, BatteryState::NORMAL);
}

TEST_F(BatteryMonitorTest, ReadSuccessFullCapped) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    ASSERT_EQ(monitor->init(), ESP_OK);

    // 2150 mV on ADC -> 4300 mV on battery (above 4.2V max)
    EXPECT_CALL(*mock_reader, read_adc_mv(_))
        .WillOnce(DoAll(SetArgReferee<0>(2150), Return(ESP_OK)));

    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_OK);
    EXPECT_EQ(reading.voltage_mv, 4300);
    EXPECT_EQ(reading.percent, 100);
    EXPECT_EQ(reading.state, BatteryState::FULL);
}

TEST_F(BatteryMonitorTest, ReadSuccessLowState) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    ASSERT_EQ(monitor->init(), ESP_OK);

    // 1675 mV on ADC -> 3350 mV on battery (between critical 3.2V and low 3.4V)
    EXPECT_CALL(*mock_reader, read_adc_mv(_))
        .WillOnce(DoAll(SetArgReferee<0>(1675), Return(ESP_OK)));

    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_OK);
    EXPECT_EQ(reading.voltage_mv, 3350);
    EXPECT_EQ(reading.percent, 29); // (3350-3000)*100/1200 = 29.1%
    EXPECT_EQ(reading.state, BatteryState::LOW);
}

TEST_F(BatteryMonitorTest, ReadSuccessCriticalState) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    ASSERT_EQ(monitor->init(), ESP_OK);

    // 1550 mV on ADC -> 3100 mV on battery (below critical 3.2V)
    EXPECT_CALL(*mock_reader, read_adc_mv(_))
        .WillOnce(DoAll(SetArgReferee<0>(1550), Return(ESP_OK)));

    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_OK);
    EXPECT_EQ(reading.voltage_mv, 3100);
    EXPECT_EQ(reading.percent, 8); // (3100-3000)*100/1200 = 8.3%
    EXPECT_EQ(reading.state, BatteryState::CRITICAL);
}

TEST_F(BatteryMonitorTest, ReadSuccessEmptyCapped) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    ASSERT_EQ(monitor->init(), ESP_OK);

    // 1450 mV on ADC -> 2900 mV on battery (below 3.0V min)
    EXPECT_CALL(*mock_reader, read_adc_mv(_))
        .WillOnce(DoAll(SetArgReferee<0>(1450), Return(ESP_OK)));

    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_OK);
    EXPECT_EQ(reading.voltage_mv, 2900);
    EXPECT_EQ(reading.percent, 0);
    EXPECT_EQ(reading.state, BatteryState::CRITICAL);
}

TEST_F(BatteryMonitorTest, ReadFailOnReaderError) {
    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    ASSERT_EQ(monitor->init(), ESP_OK);

    EXPECT_CALL(*mock_reader, read_adc_mv(_)).WillOnce(Return(ESP_FAIL));

    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_FAIL);
}

TEST_F(BatteryMonitorTest, ReadFailDividerBottomZero) {
    config_.divider_bottom_ohms = 0;
    // Re-create monitor with invalid config
    monitor = std::make_unique<BatteryMonitor>(*mock_reader, config_);

    EXPECT_CALL(*mock_reader, init()).WillOnce(Return(ESP_OK));
    ASSERT_EQ(monitor->init(), ESP_OK);

    BatteryReading reading;
    EXPECT_EQ(monitor->read(reading), ESP_ERR_INVALID_ARG);
}


// =============================================================================
// AdcBatteryReader Tests
// =============================================================================

class AdcBatteryReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_adc_oneshot = std::make_shared<NiceMock<MockHalAdcOneshot>>();
        mock_adc_cali = std::make_shared<NiceMock<MockHalAdcCalibration>>();
        mock_timer = std::make_shared<NiceMock<MockBmHalTimer>>();
        
        config_.gpio_num = 3;
        config_.sample_count = 10;
        config_.sample_delay_us = 1000;
        config_.enable_calibration = true;
        
        reader = std::make_unique<AdcBatteryReader>(*mock_adc_oneshot, *mock_adc_cali, *mock_timer, config_);
        
        fake_unit_handle = reinterpret_cast<adc_oneshot_unit_handle_t>(0x1234);
        fake_cali_handle = reinterpret_cast<adc_cali_handle_t>(0x5678);
    }

    std::shared_ptr<NiceMock<MockHalAdcOneshot>> mock_adc_oneshot;
    std::shared_ptr<NiceMock<MockHalAdcCalibration>> mock_adc_cali;
    std::shared_ptr<NiceMock<MockBmHalTimer>> mock_timer;
    BatteryAdcConfig config_;
    std::unique_ptr<AdcBatteryReader> reader;
    
    adc_oneshot_unit_handle_t fake_unit_handle;
    adc_cali_handle_t fake_cali_handle;
};

TEST_F(AdcBatteryReaderTest, InitSuccessWithCalibration) {
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));

    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_cali_handle), Return(ESP_OK)));

    EXPECT_EQ(reader->init(), ESP_OK);
    EXPECT_TRUE(reader->is_initialized());
}

TEST_F(AdcBatteryReaderTest, InitSuccessWithoutCalibration) {
    config_.enable_calibration = false;
    reader = std::make_unique<AdcBatteryReader>(*mock_adc_oneshot, *mock_adc_cali, *mock_timer, config_);

    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));

    // create_scheme_curve_fitting should NOT be called
    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _)).Times(0);

    EXPECT_EQ(reader->init(), ESP_OK);
    EXPECT_TRUE(reader->is_initialized());
}

TEST_F(AdcBatteryReaderTest, InitFailIoToChannel) {
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(reader->init(), ESP_FAIL);
    EXPECT_FALSE(reader->is_initialized());
}

TEST_F(AdcBatteryReaderTest, InitFailNewUnit) {
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(reader->init(), ESP_FAIL);
    EXPECT_FALSE(reader->is_initialized());
}

TEST_F(AdcBatteryReaderTest, InitFailConfigChannelCleansUp) {
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_FAIL));

    // Unit deletion should be called to prevent memory leaks
    EXPECT_CALL(*mock_adc_oneshot, del_unit(fake_unit_handle))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(reader->init(), ESP_FAIL);
    EXPECT_FALSE(reader->is_initialized());
}

TEST_F(AdcBatteryReaderTest, InitCalibrationFailsStillSucceeds) {
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));

    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));

    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _))
        .WillOnce(Return(ESP_ERR_NOT_SUPPORTED));

    // Reader initialization should succeed even if calibration fails
    EXPECT_EQ(reader->init(), ESP_OK);
    EXPECT_TRUE(reader->is_initialized());
}

TEST_F(AdcBatteryReaderTest, DoubleInitReturnsInvalidState) {
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _))
        .WillOnce(Return(ESP_ERR_NOT_SUPPORTED));

    ASSERT_EQ(reader->init(), ESP_OK);
    EXPECT_EQ(reader->init(), ESP_ERR_INVALID_STATE);
}

TEST_F(AdcBatteryReaderTest, DeinitSuccess) {
    // Init first
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_cali_handle), Return(ESP_OK)));

    ASSERT_EQ(reader->init(), ESP_OK);

    // Deinit
    EXPECT_CALL(*mock_adc_cali, delete_scheme_curve_fitting(fake_cali_handle))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(*mock_adc_oneshot, del_unit(fake_unit_handle))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(reader->deinit(), ESP_OK);
    EXPECT_FALSE(reader->is_initialized());
}

TEST_F(AdcBatteryReaderTest, DeinitNotInitializedReturnsInvalidState) {
    EXPECT_EQ(reader->deinit(), ESP_ERR_INVALID_STATE);
}

TEST_F(AdcBatteryReaderTest, ReadUninitializedReturnsInvalidState) {
    uint16_t mv = 0;
    EXPECT_EQ(reader->read_adc_mv(mv), ESP_ERR_INVALID_STATE);
}

TEST_F(AdcBatteryReaderTest, ReadAdcMvSuccessWithCalibration) {
    // Setup Init
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_cali_handle), Return(ESP_OK)));

    ASSERT_EQ(reader->init(), ESP_OK);

    // Read: 10 samples, raw value 2000
    EXPECT_CALL(*mock_adc_oneshot, read(fake_unit_handle, ADC_CHANNEL_3, _))
        .Times(10)
        .WillRepeatedly(DoAll(SetArgPointee<2>(2000), Return(ESP_OK)));

    // Timer delay should be called 9 times (sample_count - 1)
    EXPECT_CALL(*mock_timer, delay_us(1000))
        .Times(9);

    // Calibration: map 2000 raw to 1200 mV
    EXPECT_CALL(*mock_adc_cali, raw_to_voltage(fake_cali_handle, 2000, _))
        .WillOnce(DoAll(SetArgPointee<2>(1200), Return(ESP_OK)));

    uint16_t out_mv = 0;
    EXPECT_EQ(reader->read_adc_mv(out_mv), ESP_OK);
    EXPECT_EQ(out_mv, 1200);
}

TEST_F(AdcBatteryReaderTest, ReadAdcMvSuccessFallbackRaw) {
    // Setup Init (Calibration fails)
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _))
        .WillOnce(Return(ESP_ERR_NOT_SUPPORTED));

    ASSERT_EQ(reader->init(), ESP_OK);

    // Read: 10 samples, raw value 2048 (approx middle of 12-bit ADC range)
    EXPECT_CALL(*mock_adc_oneshot, read(fake_unit_handle, ADC_CHANNEL_3, _))
        .Times(10)
        .WillRepeatedly(DoAll(SetArgPointee<2>(2048), Return(ESP_OK)));

    // Timer delay should be called 9 times
    EXPECT_CALL(*mock_timer, delay_us(1000))
        .Times(9);

    // Fallback formula: (raw * 2500) / 4095
    // (2048 * 2500) / 4095 = 5120000 / 4095 = 1250.3 mV -> 1250 mV
    uint16_t out_mv = 0;
    EXPECT_EQ(reader->read_adc_mv(out_mv), ESP_OK);
    EXPECT_EQ(out_mv, 1250);
}

TEST_F(AdcBatteryReaderTest, ReadAdcMvFailOnReadError) {
    // Setup Init
    EXPECT_CALL(*mock_adc_oneshot, io_to_channel(3, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(ADC_UNIT_1), SetArgPointee<2>(ADC_CHANNEL_3), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, new_unit(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_unit_handle), Return(ESP_OK)));
    EXPECT_CALL(*mock_adc_oneshot, config_channel(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(*mock_adc_cali, create_scheme_curve_fitting(_, _))
        .WillOnce(Return(ESP_ERR_NOT_SUPPORTED));

    ASSERT_EQ(reader->init(), ESP_OK);

    // Read: first is OK, second fails
    EXPECT_CALL(*mock_adc_oneshot, read(fake_unit_handle, ADC_CHANNEL_3, _))
        .WillOnce(DoAll(SetArgPointee<2>(2000), Return(ESP_OK)))
        .WillOnce(Return(ESP_FAIL));

    uint16_t out_mv = 0;
    EXPECT_EQ(reader->read_adc_mv(out_mv), ESP_FAIL);
}

} // namespace test
} // namespace battery_monitor
