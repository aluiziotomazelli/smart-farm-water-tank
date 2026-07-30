#pragma once

#include "farm_protocol_types.hpp"
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
    static constexpr uint32_t MAGIC = 0x544B; ///< For CRC validation | 0x544B = "TK"

    // Magic first (validation)
    uint16_t magic = MAGIC;

    // --- Current State ---
    uint16_t level_permille = 0;
    uint16_t last_level_permille = 0;
    FillState fill_state = FillState::UNKNOWN;
    // It is no longer used for fill state logic (replaced by last_level_permille).
    float last_distance_cm = 0.0f;
    ultrasonic::UsResult last_result = ultrasonic::UsResult::HW_FAULT;
    uint64_t sample_timestamp_ms = 0;

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

    // --- Battery Stats ---
    uint16_t last_battery_mv = 0;
    uint8_t last_battery_percent = 0;
    farm::BatteryState last_battery_state = farm::BatteryState::UNKNOWN;

    // CRC MUST BE LAST of the validated fields
    uint32_t crc = 0;

    // Below: excluded from CRC calculation (after crc), but included in
    // operator== so it persists in RTC across deep sleep cycles.
    uint32_t cycles_since_nvs_commit = 0;

    void reset()
    {
        *this = {};
        magic = MAGIC;
    }

    bool operator==(const WaterTankStats& other) const
    {
        return level_permille == other.level_permille && last_level_permille == other.last_level_permille &&
               fill_state == other.fill_state && last_distance_cm == other.last_distance_cm &&
               last_result == other.last_result && sample_timestamp_ms == other.sample_timestamp_ms &&
               measure_count == other.measure_count && ok_count == other.ok_count && weak_count == other.weak_count &&
               timeout_count == other.timeout_count && out_of_range_count == other.out_of_range_count &&
               high_variance_count == other.high_variance_count &&
               insufficient_samples_count == other.insufficient_samples_count &&
               echo_stuck_count == other.echo_stuck_count && hw_fault_count == other.hw_fault_count &&
               gpio_wakeup_enabled == other.gpio_wakeup_enabled && backup_mode_active == other.backup_mode_active &&
               consecutive_failures == other.consecutive_failures && last_battery_mv == other.last_battery_mv &&
               last_battery_percent == other.last_battery_percent && last_battery_state == other.last_battery_state &&
               cycles_since_nvs_commit == other.cycles_since_nvs_commit;
    }

    bool operator!=(const WaterTankStats& other) const { return !(*this == other); }
};
