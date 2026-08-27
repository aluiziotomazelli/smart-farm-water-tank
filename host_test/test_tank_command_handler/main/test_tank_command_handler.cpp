// host_test/test_tank_command_handler/main/test_tank_command_handler.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

#include "tank_command_handler.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_time_manager.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_freertos.hpp"
#include "farm_protocol_types.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class TankCommandHandlerTest : public ::testing::Test
{
protected:
    NiceMock<espnow::MockEspNowManager> espnow_;
    NiceMock<time_manager::MockTimeManager> time_manager_;
    NiceMock<idf_hals::MockTimerHAL> sys_timer_;
    NiceMock<idf_hals::MockHalFreertos> hal_rtos_;

    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x1234);
    uint64_t current_time_us_{1000000ULL}; // 1 second

    std::unique_ptr<TankCommandHandler> sut_;

    void SetUp() override
    {
        ON_CALL(espnow_, confirm_reception(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(time_manager_, sync_from_time_packet(_)).WillByDefault(Return(ESP_OK));

        ON_CALL(sys_timer_, get_time_us()).WillByDefault([this]() {
            return current_time_us_;
        });

        sut_ = std::make_unique<TankCommandHandler>(
            dummy_queue_,
            espnow_,
            time_manager_,
            sys_timer_,
            hal_rtos_);
    }

    void SetupQueueWithMessages(const std::vector<espnow::AppMessage>& messages)
    {
        auto it = std::make_shared<size_t>(0);
        auto msgs = std::make_shared<std::vector<espnow::AppMessage>>(messages);

        ON_CALL(hal_rtos_, queue_receive(dummy_queue_, _, _))
            .WillByDefault([this, it, msgs](QueueHandle_t, void* buf, TickType_t) -> BaseType_t {
                if (*it < msgs->size()) {
                    std::memcpy(buf, &(*msgs)[*it], sizeof(espnow::AppMessage));
                    (*it)++;
                    return pdTRUE;
                }
                current_time_us_ += 1000000ULL; // advance past deadline
                return pdFALSE;
            });
    }
};

TEST_F(TankCommandHandlerTest, NullQueueReturnsDefaultResult)
{
    TankCommandHandler null_handler(
        nullptr,
        espnow_,
        time_manager_,
        sys_timer_,
        hal_rtos_);

    auto res = null_handler.process(100);
    EXPECT_FALSE(res.time_synced);
    EXPECT_FALSE(res.ota_requested);
    EXPECT_FALSE(res.reboot_requested);
    EXPECT_EQ(res.override_sleep_us, 0);
}

TEST_F(TankCommandHandlerTest, EmptyQueueReturnsDefaultResult)
{
    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, _))
        .WillRepeatedly([this](QueueHandle_t, void*, TickType_t) {
            current_time_us_ += 1000000ULL;
            return pdFALSE;
        });

    auto res = sut_->process(100);
    EXPECT_FALSE(res.time_synced);
    EXPECT_FALSE(res.ota_requested);
    EXPECT_FALSE(res.reboot_requested);
    EXPECT_EQ(res.override_sleep_us, 0);
}

TEST_F(TankCommandHandlerTest, ProcessStartOtaCommandSetsFlagAndAcks)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    msg.sequence_number = 42;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 42, espnow::AckStatus::OK)).Times(1);

    auto res = sut_->process(100);
    EXPECT_TRUE(res.ota_requested);
    EXPECT_FALSE(res.reboot_requested);
}

TEST_F(TankCommandHandlerTest, ProcessRebootCommandSetsFlagAndAcks)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::REBOOT);
    msg.sequence_number = 100;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 100, espnow::AckStatus::OK)).Times(1);

    auto res = sut_->process(100);
    EXPECT_TRUE(res.reboot_requested);
}

TEST_F(TankCommandHandlerTest, ProcessSleepOverrideCommandSetsOverrideAndAcks)
{
    farm::SleepOverrideCommand cmd{.sleep_time_s = 120};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SLEEP_OVERRIDE);
    msg.payload_len = sizeof(cmd);
    std::memcpy(msg.payload, &cmd, sizeof(cmd));
    msg.sequence_number = 55;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 55, espnow::AckStatus::OK)).Times(1);

    auto res = sut_->process(100);
    EXPECT_EQ(res.override_sleep_us, 120000000ULL);
}

TEST_F(TankCommandHandlerTest, ProcessSleepOverrideInvalidPayloadLengthSendsErrorInvalidData)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SLEEP_OVERRIDE);
    msg.payload_len = 1; // Corrupted length
    msg.sequence_number = 12;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 12, espnow::AckStatus::ERROR_INVALID_DATA)).Times(1);

    auto res = sut_->process(100);
    EXPECT_EQ(res.override_sleep_us, 0);
}

TEST_F(TankCommandHandlerTest, ProcessSyncTimeCommandSynchronizesTimeAndReturnsTimeSynced)
{
    farm::TimeSyncCommand sync_cmd{
        .timestamp_ms = 1700000000000ULL,
        .tz_offset_min = -180,
        .sync_source = 3,
        .flags = 1};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SYNC_TIME);
    msg.payload_len = sizeof(sync_cmd);
    std::memcpy(msg.payload, &sync_cmd, sizeof(sync_cmd));
    msg.sequence_number = 99;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(time_manager_, sync_from_time_packet(_))
        .WillOnce([](const time_manager::TimeSyncPacket& pkt) {
            EXPECT_EQ(pkt.timestamp_ms, 1700000000000ULL);
            EXPECT_EQ(pkt.tz_offset_min, -180);
            EXPECT_EQ(pkt.sync_source, time_manager::TimeSyncSource::ESP_NOW);
            EXPECT_EQ(pkt.flags, 1);
            return ESP_OK;
        });
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 99, espnow::AckStatus::OK)).Times(1);

    auto res = sut_->process(100);
    EXPECT_TRUE(res.time_synced);
}

TEST_F(TankCommandHandlerTest, ProcessSyncTimeFailureSendsErrorProcessing)
{
    farm::TimeSyncCommand sync_cmd{
        .timestamp_ms = 1700000000000ULL,
        .tz_offset_min = -180,
        .sync_source = 3,
        .flags = 1};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SYNC_TIME);
    msg.payload_len = sizeof(sync_cmd);
    std::memcpy(msg.payload, &sync_cmd, sizeof(sync_cmd));
    msg.sequence_number = 77;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(time_manager_, sync_from_time_packet(_)).WillOnce(Return(ESP_ERR_INVALID_STATE));
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 77, espnow::AckStatus::ERROR_PROCESSING)).Times(1);

    auto res = sut_->process(100);
    EXPECT_FALSE(res.time_synced);
}

TEST_F(TankCommandHandlerTest, ProcessSyncTimeInvalidPayloadLengthSendsErrorInvalidData)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SYNC_TIME);
    msg.payload_len = 2; // Invalid length
    msg.sequence_number = 88;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 88, espnow::AckStatus::ERROR_INVALID_DATA)).Times(1);

    auto res = sut_->process(100);
    EXPECT_FALSE(res.time_synced);
}

TEST_F(TankCommandHandlerTest, UnknownGenericCommandSendsErrorProcessing)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = 0x30; // Unknown generic command
    msg.sequence_number = 13;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 13, espnow::AckStatus::ERROR_PROCESSING)).Times(1);

    sut_->process(100);
}

TEST_F(TankCommandHandlerTest, UnknownFarmCommandSendsErrorProcessing)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = 0xFE; // Unknown farm command
    msg.sequence_number = 14;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 14, espnow::AckStatus::ERROR_PROCESSING)).Times(1);

    sut_->process(100);
}

TEST_F(TankCommandHandlerTest, NonBlockingProcessDrainsQueueWhenTimeoutIsZero)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    msg.sequence_number = 15;
    msg.requires_ack = false;

    auto it = std::make_shared<size_t>(0);
    ON_CALL(hal_rtos_, queue_receive(dummy_queue_, _, 0))
        .WillByDefault([msg, it](QueueHandle_t, void* buf, TickType_t) -> BaseType_t {
            if (*it == 0) {
                std::memcpy(buf, &msg, sizeof(msg));
                (*it)++;
                return pdTRUE;
            }
            return pdFALSE;
        });

    auto res = sut_->process(0);
    EXPECT_TRUE(res.ota_requested);
}
