// main/include/ota_controller.hpp
#pragma once

#include "esp_err.h"
#include "ota_manager.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_wifi_manager.hpp"

/**
 * @brief Configuration for the OtaController.
 */
struct OtaControllerConfig
{
    uint32_t wifi_connect_timeout_ms;  ///< Max wait for WiFi IP acquisition
    uint32_t trigger_poll_interval_ms; ///< Polling interval for the trigger
    uint32_t trigger_timeout_ms;       ///< 0 = wait forever; >0 = give up after N ms
};

/**
 * @brief Orchestrates the OTA update flow for the Water Tank application.
 *
 * Responsibilities:
 * - On boot: check if the new firmware needs confirmation (pending verify).
 * - When running: poll the trigger, connect WiFi, invoke OtaManager::start_ota().
 *
 * The rollback policy (what counts as a valid boot) is app-specific and
 * defined here, keeping it decoupled from the generic OtaManager.
 */
class OtaController
{
public:
    OtaController(
        OtaManager& ota_manager,
        wifi_manager::IWiFiManager& wifi,
        IOtaTrigger& trigger,
        const OtaControllerConfig& config);

    /**
     * @brief Check and handle a firmware pending-verification state.
     *
     * If the current firmware is in PENDING_VERIFY state (just rebooted after OTA),
     * confirms it as valid. If confirmation fails, triggers rollback and reboot.
     *
     * Call this ONCE at startup, before the main application loop.
     */
    void handle_pending_verify();

    /**
     * @brief Poll the trigger and, if fired, connect WiFi and run OTA.
     *
     * Blocks until the trigger fires or trigger_timeout_ms elapses.
     *
     * @return ESP_OK        OTA started successfully (device will reboot).
     * @return ESP_ERR_TIMEOUT trigger window expired, no OTA performed.
     * @return ESP_FAIL      WiFi connection or OTA start failed.
     */
    esp_err_t wait_and_run();

private:
    OtaManager& ota_manager_;
    wifi_manager::IWiFiManager& wifi_;
    IOtaTrigger& trigger_;
    OtaControllerConfig config_;

    esp_err_t connect_wifi();
};
