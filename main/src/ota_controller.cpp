// main/src/ota_controller.cpp
#include "ota_controller.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "wifi_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "OtaController";

OtaController::OtaController(
    OtaManager& ota_manager,
    wifi_manager::IWiFiManager& wifi,
    IOtaTrigger& trigger,
    const OtaControllerConfig& config)
    : ota_manager_(ota_manager)
    , wifi_(wifi)
    , trigger_(trigger)
    , config_(config)
{
}

void OtaController::handle_pending_verify()
{
    if (!ota_manager_.check_pending_verify()) {
        return;
    }

    ESP_LOGI(TAG, "New firmware pending verification. Confirming as valid.");
    if (ota_manager_.confirm_app_valid()) {
        ESP_LOGI(TAG, "Firmware confirmed successfully.");
    }
    else {
        ESP_LOGE(TAG, "Failed to confirm firmware. Triggering rollback.");
        ota_manager_.rollback_and_reboot();
    }
}

esp_err_t OtaController::wait_and_run()
{
    ESP_LOGI(TAG, "OTA window open. Waiting for trigger (%lu ms)...", config_.trigger_timeout_ms);

    uint32_t elapsed_ms = 0;
    while (true) {
        if (trigger_.is_triggered()) {
            ESP_LOGI(TAG, "OTA trigger received.");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(config_.trigger_poll_interval_ms));
        elapsed_ms += config_.trigger_poll_interval_ms;

        if (config_.trigger_timeout_ms > 0 && elapsed_ms >= config_.trigger_timeout_ms) {
            ESP_LOGI(TAG, "OTA trigger window expired. Proceeding with normal boot.");
            return ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t err = connect_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed before OTA: %s", esp_err_to_name(err));
        return err;
    }

    if (!ota_manager_.start_ota()) {
        ESP_LOGE(TAG, "Failed to start OTA process.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA in progress. Waiting for completion...");
    while (true) {
        OtaStatus status = ota_manager_.get_status();
        if (status == OtaStatus::READY_TO_RESTART || status == OtaStatus::FAILED) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (ota_manager_.get_status() == OtaStatus::FAILED) {
        ESP_LOGE(TAG, "OTA failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete. Device will reboot.");
    return ESP_OK;
}

esp_err_t OtaController::connect_wifi()
{
    wifi_manager::State state = wifi_.get_state();

    if (state == wifi_manager::State::CONNECTED_GOT_IP) {
        ESP_LOGI(TAG, "WiFi already connected.");
        return ESP_OK;
    }

    if (state == wifi_manager::State::STARTED) {
        ESP_LOGI(TAG, "WiFi started, connecting...");
        return wifi_.connect(config_.wifi_connect_timeout_ms);
    }

    ESP_LOGE(TAG, "WiFi not in a connectable state: %d", static_cast<int>(state));
    return ESP_ERR_INVALID_STATE;
}
