#include "water_tank_logic.hpp"
#include "esp_log.h"

static const char* TAG = "WaterTankLogic";

WaterTankLogic::WaterTankLogic(const TankGeometry& geometry, floatswitch::IFloatSwitch& float_switch)
    : geometry_(geometry)
    , float_switch_(float_switch)
{
}

void WaterTankLogic::process_reading(const ultrasonic::Reading& reading, WaterTankStats& stats)
{
    stats.last_result = reading.result;
    stats.measure_count++;

    update_results_counters(reading.result, stats);

    if (ultrasonic::is_success(reading.result)) {
        stats.last_distance_cm = reading.cm;
        stats.level_permille = geometry_.calculate_permille(reading.cm);
        update_fill_state(stats.level_permille, stats);
        stats.last_level_permille = stats.level_permille; // Store for next iteration
    }
}

void WaterTankLogic::update_operation_mode(WaterTankStats& stats) const
{
    if (!ultrasonic::is_success(stats.last_result)) {
        if (stats.consecutive_failures < CONSECUTIVE_FAILURES_THRESHOLD) {
            stats.consecutive_failures++;
        }
    }
    else if (stats.last_result == ultrasonic::UsResult::OK) {
        if (stats.consecutive_failures > 0) {
            stats.consecutive_failures--;
        }
    }

    if (stats.consecutive_failures >= CONSECUTIVE_FAILURES_THRESHOLD) {
        stats.backup_mode_active = true;
    }
    else if (stats.consecutive_failures < CONSECUTIVE_FAILURES_THRESHOLD - 1) {
        stats.backup_mode_active = false;
    }
}

uint64_t WaterTankLogic::calculate_sleep_time_us(const WaterTankStats& stats) const
{
    if (stats.backup_mode_active) {
        if (!float_switch_.is_tank_full()) {
            return BACKUP_MODE_SLEEP_US;
        }
        else {
            return TIMER_STABLE_US;
        }
    }

    uint64_t timer_us = 0;
    if (ultrasonic::is_success(stats.last_result)) {
        switch (stats.fill_state) {
        case FillState::FILLING:
            timer_us = TIMER_FILLING_US;
            break;
        case FillState::DRAINING:
            timer_us = TIMER_DRAIN_US;
            break;
        case FillState::STABLE:
            timer_us = TIMER_STABLE_US;
            break;
        default:
            timer_us = TIMER_UNKNOWN_US;
            break;
        }

        if (stats.last_result == ultrasonic::UsResult::WEAK_SIGNAL) {
            timer_us *= WEAK_SLEEP_FACTOR;
        }
    }
    else {
        timer_us = TIMER_UNKNOWN_US * INVALID_SLEEP_FACTOR;
    }

    return timer_us;
}

// =====================================================================
// PRIVATE METHODS
// =====================================================================

void WaterTankLogic::update_fill_state(uint16_t current_permille, WaterTankStats& stats) const
{
    if (stats.last_level_permille == 0) {
        stats.fill_state = FillState::STABLE;
        return;
    }

    int32_t delta = static_cast<int32_t>(current_permille) - static_cast<int32_t>(stats.last_level_permille);
    uint32_t abs_delta = (delta < 0) ? -delta : delta;

    if (abs_delta < LEVEL_DELTA_MIN) {
        stats.fill_state = FillState::STABLE;
    }
    else if (delta > 0) {
        stats.fill_state = FillState::FILLING;
    }
    else {
        stats.fill_state = FillState::DRAINING;
    }
}

void WaterTankLogic::update_results_counters(ultrasonic::UsResult result, WaterTankStats& stats)
{
    switch (result) {
    case ultrasonic::UsResult::OK:
        stats.ok_count++;
        break;
    case ultrasonic::UsResult::WEAK_SIGNAL:
        stats.weak_count++;
        break;
    case ultrasonic::UsResult::TIMEOUT:
        stats.timeout_count++;
        break;
    case ultrasonic::UsResult::OUT_OF_RANGE:
        stats.out_of_range_count++;
        break;
    case ultrasonic::UsResult::HIGH_VARIANCE:
        stats.high_variance_count++;
        break;
    case ultrasonic::UsResult::INSUFFICIENT_SAMPLES:
        stats.insufficient_samples_count++;
        break;
    case ultrasonic::UsResult::ECHO_STUCK:
        stats.echo_stuck_count++;
        break;
    case ultrasonic::UsResult::HW_FAULT:
        stats.hw_fault_count++;
        break;
    }
}