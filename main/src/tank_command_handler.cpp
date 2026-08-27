// main/src/tank_command_handler.cpp
#include "tank_command_handler.hpp"

#include <cstring>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "farm_protocol_types.hpp"

static const char* TAG = "TankCommandHandler";

TankCommandHandler::TankCommandHandler(
    QueueHandle_t rx_queue,
    espnow::IEspNowManager& espnow,
    time_manager::ITimeManager& time_manager,
    idf_hals::ITimerHAL& sys_timer,
    idf_hals::IHalFreertos& rtos)
    : rx_queue_(rx_queue)
    , espnow_(espnow)
    , time_manager_(time_manager)
    , sys_timer_(sys_timer)
    , rtos_(rtos)
{
}

TankCommandProcessResult TankCommandHandler::process(uint32_t timeout_ms)
{
    TankCommandProcessResult result{};

    if (rx_queue_ == nullptr) {
        if (timeout_ms > 0) {
            rtos_.task_delay(pdMS_TO_TICKS(timeout_ms));
        }
        return result;
    }

    if (timeout_ms == 0) {
        espnow::AppMessage msg{};
        while (rtos_.queue_receive(rx_queue_, &msg, 0) == pdPASS) {
            if (msg.msg_type == espnow::MessageType::COMMAND) {
                process_command_message(msg, result);
            }
        }
        return result;
    }

    int64_t deadline_ms = (sys_timer_.get_time_us() / 1000) + timeout_ms;
    espnow::AppMessage msg{};

    while ((sys_timer_.get_time_us() / 1000) < deadline_ms) {
        int64_t remaining = deadline_ms - (sys_timer_.get_time_us() / 1000);
        if (remaining <= 0) {
            break;
        }

        if (rtos_.queue_receive(rx_queue_, &msg, pdMS_TO_TICKS(remaining)) == pdPASS) {
            if (msg.msg_type == espnow::MessageType::COMMAND) {
                process_command_message(msg, result);
            }
        }
    }

    return result;
}

void TankCommandHandler::send_cmd_ack(const espnow::AppMessage& msg, espnow::AckStatus status)
{
    if (msg.requires_ack) {
        espnow_.confirm_reception(msg.sender_id, msg.sequence_number, status);
    }
}

void TankCommandHandler::process_command_message(const espnow::AppMessage& msg, TankCommandProcessResult& result)
{
    // Generic transport commands (0x01–0x3F)
    if (msg.payload_type <= 0x3F) {
        switch (static_cast<espnow::CommandType>(msg.payload_type)) {
        case espnow::CommandType::START_OTA:
            ESP_LOGW(TAG, "Received START_OTA command from Hub - triggering OTA");
            result.ota_requested = true;
            send_cmd_ack(msg, espnow::AckStatus::OK);
            break;

        case espnow::CommandType::REBOOT:
            ESP_LOGW(TAG, "Received REBOOT command from Hub");
            result.reboot_requested = true;
            send_cmd_ack(msg, espnow::AckStatus::OK);
            break;

        default:
            ESP_LOGW(TAG, "Unknown generic command: 0x%02X", msg.payload_type);
            send_cmd_ack(msg, espnow::AckStatus::ERROR_PROCESSING);
            break;
        }
    }
    // Farm application commands (0x40–0xFF)
    else {
        switch (static_cast<farm::CommandType>(msg.payload_type)) {
        case farm::CommandType::SLEEP_OVERRIDE:
            if (msg.payload_len >= sizeof(farm::SleepOverrideCommand)) {
                farm::SleepOverrideCommand sleep_cmd{};
                std::memcpy(&sleep_cmd, msg.payload, sizeof(sleep_cmd));
                result.override_sleep_us = static_cast<uint64_t>(sleep_cmd.sleep_time_s) * 1000000ULL;
                ESP_LOGI(TAG, "Received SLEEP_OVERRIDE: %lu s", static_cast<unsigned long>(sleep_cmd.sleep_time_s));
                send_cmd_ack(msg, espnow::AckStatus::OK);
            } else {
                ESP_LOGE(TAG, "Invalid payload length for SLEEP_OVERRIDE: %zu", msg.payload_len);
                send_cmd_ack(msg, espnow::AckStatus::ERROR_INVALID_DATA);
            }
            break;

        case farm::CommandType::SYNC_TIME:
            if (msg.payload_len >= sizeof(farm::TimeSyncCommand)) {
                farm::TimeSyncCommand farm_cmd{};
                std::memcpy(&farm_cmd, msg.payload, sizeof(farm_cmd));
                time_manager::TimeSyncPacket pkt{};
                pkt.timestamp_ms = farm_cmd.timestamp_ms;
                pkt.tz_offset_min = farm_cmd.tz_offset_min;
                pkt.sync_source = time_manager::TimeSyncSource::ESP_NOW;
                pkt.flags = farm_cmd.flags;

                esp_err_t err = time_manager_.sync_from_time_packet(pkt);
                if (err == ESP_OK) {
                    result.time_synced = true;
                    ESP_LOGI(TAG, "Time synch from ESP-NOW: %llu ms", static_cast<unsigned long long>(pkt.timestamp_ms));
                    send_cmd_ack(msg, espnow::AckStatus::OK);
                } else {
                    ESP_LOGE(TAG, "Time sync failed: %s", esp_err_to_name(err));
                    send_cmd_ack(msg, espnow::AckStatus::ERROR_PROCESSING);
                }
            } else {
                ESP_LOGE(TAG, "Invalid payload length for SYNC_TIME: %zu", msg.payload_len);
                send_cmd_ack(msg, espnow::AckStatus::ERROR_INVALID_DATA);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown farm command: 0x%02X", msg.payload_type);
            send_cmd_ack(msg, espnow::AckStatus::ERROR_PROCESSING);
            break;
        }
    }
}
