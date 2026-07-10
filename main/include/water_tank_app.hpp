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

/**
 * @class WaterTankApp
 * @brief Orchestrator for the Water Tank monitoring application.
 */
class WaterTankApp
{
public:
    /** @brief Constructor for testing (dependency injection) */
    WaterTankApp(
        ILevelSensor& sensor,
        floatswitch::IFloatSwitch& float_switch,
        IWaterTankStorage& storage,
        espnow::IEspNowManager& comm,
        power_control::IPowerControl& power,
        ISleepHAL& sleep,
        battery_monitor::IBatteryMonitor& battery_monitor,
        WaterTankLogic& logic);

    /**
     * @brief Execute the main application loop.
     */
    void run();

private:
    ILevelSensor& sensor_;
    floatswitch::IFloatSwitch& float_switch_;
    IWaterTankStorage& storage_;
    espnow::IEspNowManager& comm_;
    power_control::IPowerControl& power_;
    ISleepHAL& sleep_;
    battery_monitor::IBatteryMonitor& battery_monitor_;
    WaterTankLogic& logic_;

    WaterTankStats stats_;

    esp_err_t send_report();
    SensorStatus map_status(ultrasonic::UsResult result);
    void enter_deep_sleep(uint64_t sleep_time_us);
};
