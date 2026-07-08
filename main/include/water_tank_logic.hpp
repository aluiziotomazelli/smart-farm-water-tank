#pragma once

#include "interfaces/i_float_switch.hpp"
#include "tank_geometry.hpp"
#include "water_tank_stats.hpp"
#include "water_tank_types.hpp"

/**
 * @class WaterTankLogic
 * @brief The core business logic of the water tank system.
 */
class WaterTankLogic
{
public:
    WaterTankLogic(const TankGeometry& geometry, floatswitch::IFloatSwitch& float_switch);

    void process_reading(const ultrasonic::Reading& reading, WaterTankStats& stats);

    uint64_t calculate_sleep_time_us(const WaterTankStats& stats) const;

    void update_operation_mode(WaterTankStats& stats) const;

private:
    const TankGeometry& geometry_;
    floatswitch::IFloatSwitch& float_switch_;

    void update_fill_state(uint16_t current_permille, WaterTankStats& stats) const;
    void update_results_counters(ultrasonic::UsResult result, WaterTankStats& stats);
};
