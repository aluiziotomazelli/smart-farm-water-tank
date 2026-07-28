#pragma once

#include "interfaces/i_hal_sleep.hpp"
#include "interfaces/i_level_sensor.hpp"
#include "i_espnow_manager.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_water_tank_nvs.hpp"
#include "interfaces/i_power_control.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "water_tank_logic.hpp"
#include "water_tank_stats.hpp"
#include "i_float_switch.hpp" // Adding component include
#include "interfaces/i_battery_monitor.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include <atomic>
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_hal_system.hpp"

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
        idf_hals::ISystemHAL& system_hal);

    /**
     * @brief Initialize application state, dependencies and check OTA status.
     * @param is_logging If true, connects WiFi, sets ESP-NOW channel policy to FIXED, and initializes UDP logger.
     * @return ESP_OK if initialization succeeded.
     */
    esp_err_t init(bool is_logging = false);

    /**
     * @brief Execute the main application loop.
     */
    void run();

    /** @copydoc IOtaTriggerListener::on_ota_triggered */
    void on_ota_triggered(OtaTriggerSource source) override;

private:
    INvsCore& core_storage_;
    IWaterTankNvs& tank_storage_;
    ILevelSensor& sensor_;
    floatswitch::IFloatSwitch& float_switch_;
    espnow::IEspNowManager& comm_;
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

    std::atomic<bool> ota_triggered_{false};

    WaterTankStats stats_;
    CoreStorage core_;

    bool session_healthy_ = true;
    bool pending_firmware_verify_ = false;
    bool pending_core_commit_ = false;
    bool pending_tank_commit_ = false;

    esp_err_t send_report();
    farm::SensorStatus map_status(ultrasonic::UsResult result);
    void enter_deep_sleep(uint64_t sleep_time_us);
    uint64_t listen_for_messages(uint32_t timeout_ms);
    void wait_for_comm_ready(uint32_t timeout_ms);
    void process_pending_ota();
    esp_err_t disconnect_stop_wifi();
    bool process_command(const espnow::AppMessage& msg, uint64_t& out_override_sleep_us);

    esp_err_t init_wifi();
    esp_err_t init_espnow();
    esp_err_t init_ota_manager();
    esp_err_t init_core_storage();
    esp_err_t create_default_core_storage();
    void process_boot_reasons();
    void process_wakeup_cause();
    esp_err_t init_tank_storage();
    esp_err_t create_default_tank_storage();
    void check_firmware();
    void init_logger();
};
