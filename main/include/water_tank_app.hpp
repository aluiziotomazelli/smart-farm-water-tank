#pragma once

#include "interfaces/i_hal_sleep.hpp"
#include "interfaces/i_level_sensor.hpp"
#include "interfaces/i_water_tank_storage.hpp"
#include "i_espnow_manager.hpp"
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
        ILevelSensor& sensor,
        floatswitch::IFloatSwitch& float_switch,
        IWaterTankStorage& storage,
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
     * @brief Execute the main application loop.
     */
    void run();

    /** @copydoc IOtaTriggerListener::on_ota_triggered */
    void on_ota_triggered(OtaTriggerSource source) override;

private:
    ILevelSensor& sensor_;
    floatswitch::IFloatSwitch& float_switch_;
    IWaterTankStorage& storage_;
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

    esp_err_t send_report();
    farm::SensorStatus map_status(ultrasonic::UsResult result);
    void enter_deep_sleep(uint64_t sleep_time_us);
    uint64_t listen_for_commands(uint32_t timeout_ms);
    void process_pending_ota();
    esp_err_t disconnect_stop_wifi();
};
