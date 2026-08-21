#pragma once

#include <atomic>
#include <optional>

#include "interfaces/i_hal_sleep.hpp"
#include "interfaces/i_level_sensor.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "i_espnow_manager.hpp"
#include "interfaces/i_water_tank_nvs.hpp"
#include "interfaces/i_power_control.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "i_float_switch.hpp" // Adding component include
#include "interfaces/i_battery_monitor.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_hal_system.hpp"
#include "interfaces/i_time_manager.hpp"

#include "water_tank_logic.hpp"
#include "water_tank_stats.hpp"

/**
 * @class WaterTankApp
 * @brief Orchestrator for the Water Tank monitoring application.
 */
class WaterTankApp : public IOtaTriggerListener
{
public:
    /** @brief Constructor for testing (dependency injection) */
    WaterTankApp(
        INvsCore& core_storage,
        IWaterTankNvs& tank_storage,
        ILevelSensor& sensor,
        floatswitch::IFloatSwitch& float_switch,
        espnow::IEspNowManager& comm,
        QueueHandle_t rx_queue,
        power_control::IPowerControl& power,
        idf_hals::ISleepHAL& sleep,
        battery_monitor::IBatteryMonitor& battery_monitor,
        idf_hals::ITimerHAL& sys_timer,
        idf_hals::IHalFreertos& rtos,
        WaterTankLogic& logic,
        wifi_manager::IWiFiManager& wifi,
        IOtaManager& ota_manager,
        IOtaTrigger& btn_trigger,
        IOtaTrigger& espnow_trigger,
        idf_hals::ISystemHAL& system_hal,
        time_manager::ITimeManager& time_manager);

    /**
     * @brief Initialize application state, dependencies and check OTA status.
     * @param is_logging If true, connects WiFi, sets ESP-NOW channel policy to FIXED, and initializes UDP logger.
     * @return ESP_OK if initialization succeeded.
     */
    esp_err_t init(bool is_logging = false);

    /**
     * @brief Execute the main application loop.
     */
    bool run(bool enter_sleep = true);

    /** @copydoc IOtaTriggerListener::on_ota_triggered */
    void on_ota_triggered(OtaTriggerSource source) override;

private:
    INvsCore& core_storage_;
    IWaterTankNvs& tank_storage_;
    ILevelSensor& sensor_;
    floatswitch::IFloatSwitch& float_switch_;
    espnow::IEspNowManager& espnow_;
    QueueHandle_t rx_queue_;
    power_control::IPowerControl& power_;
    idf_hals::ISleepHAL& sleep_;
    battery_monitor::IBatteryMonitor& battery_monitor_;
    idf_hals::ITimerHAL& sys_timer_;
    idf_hals::IHalFreertos& rtos_;
    WaterTankLogic& logic_;
    wifi_manager::IWiFiManager& wifi_;
    IOtaManager& ota_manager_;
    IOtaTrigger& btn_trigger_;
    IOtaTrigger& espnow_trigger_;
    idf_hals::ISystemHAL& system_hal_;
    time_manager::ITimeManager& time_manager_;

    std::atomic<bool> ota_triggered_{false};

protected:
    WaterTankStats stats_;
    CoreData core_;

    bool session_healthy_ = true;
    bool pending_core_commit_ = false;
    bool pending_tank_commit_ = false;

    bool floatswitch_tank_full_ = false;

protected:
    void process_node_state();
    farm::WaterLevelReport create_report() const;
    esp_err_t send_report(const farm::WaterLevelReport& report);

    void retry_reading_if_needed(ultrasonic::Reading& reading);
    bool wait_for_comm_ready(uint32_t timeout_ms);
    void wait_for_pairing(uint32_t timeout_ms);
    uint64_t listen_for_messages(uint32_t timeout_ms);
    void process_pending_ota();
    void enter_deep_sleep(uint64_t sleep_time_us);
    void save_persistent_state();
    void process_command(const espnow::AppMessage& msg, uint64_t& out_override_sleep_us);
    void send_cmd_ack(const espnow::AppMessage& msg, espnow::AckStatus status);
    esp_err_t send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code = farm::OtaErrorCode::NONE);
    void report_ota_failure_and_restore_comm(farm::OtaErrorCode err_code, bool connected_by_us);

    esp_err_t init_wifi();
    esp_err_t init_time_manager();
    esp_err_t init_espnow();
    esp_err_t init_ota_manager();
    esp_err_t init_core_storage();
    void sync_time_from_espnow_packet(const farm::TimeSyncCommand& cmd);

    esp_err_t init_tank_storage();
    void update_running_version();
    void check_firmware_healthy();
};