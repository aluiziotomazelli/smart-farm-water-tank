#pragma once

#include "core_types.hpp"
#include "us_types.hpp"
#include "water_tank_types.hpp"

/**
 * @struct WaterTankStats
 * @brief Persistent and runtime statistics for the Water Tank application.
 *
 * This structure tracks the current state of the tank and accumulated
 * measurement statistics to help with diagnostics and logic decisions.
 */
struct WaterTankStats
{
    // --- Current State ---
    uint16_t level_permille = 0;
    uint16_t last_level_permille = 0;
    FillState fill_state = FillState::UNKNOWN;
    // TODO: Evaluate if last_distance_cm is still useful in production.
    // It is no longer used for fill state logic (replaced by last_level_permille).
    float last_distance_cm = 0.0f;
    ultrasonic::UsResult last_result = ultrasonic::UsResult::HW_FAULT;
    uint32_t sample_uptime_s = 0;

    // --- Counters ---
    uint32_t measure_count = 0;
    
    // Successes
    uint32_t ok_count = 0;
    uint32_t weak_count = 0;
    
    // Logical Failures
    uint32_t timeout_count = 0;
    uint32_t out_of_range_count = 0;
    uint32_t high_variance_count = 0;
    uint32_t insufficient_samples_count = 0;
    
    // Hardware Failures
    uint32_t echo_stuck_count = 0;
    uint32_t hw_fault_count = 0;

    // Wake / sleep info
    bool gpio_wakeup_enabled = false;

    // --- Backup Mode ---
    bool backup_mode_active = false;
    uint8_t consecutive_failures = 0;

    void reset() { *this = {}; }
};
