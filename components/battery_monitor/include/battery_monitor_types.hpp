// components/battery_monitor/include/battery_monitor_types.hpp
#pragma once

#include <cstdint>

namespace battery_monitor {

/**
 * @brief Represents the state of the battery based on voltage thresholds.
 */
enum class BatteryState : uint8_t
{
    UNKNOWN,
    CRITICAL,
    LOW,
    NORMAL,
    FULL,
};

/**
 * @brief Configuration for battery hardware voltage divider and thresholds.
 */
struct BatteryMonitorConfig
{
    uint32_t divider_top_ohms = 240000;    ///< Resistor value connected to Battery positive terminal (ohms)
    uint32_t divider_bottom_ohms = 240000; ///< Resistor value connected to Ground (ohms)
    uint16_t empty_mv = 3000;              ///< Voltage threshold representing 0% battery (millivolts)
    uint16_t full_mv = 4200;               ///< Voltage threshold representing 100% battery (millivolts)
    uint16_t low_mv = 3400;                ///< Voltage threshold representing low battery level (millivolts)
    uint16_t critical_mv = 3200;           ///< Voltage threshold representing critical battery level (millivolts)
};

/**
 * @brief Configuration for battery ADC reader sampling and calibration.
 */
struct BatteryAdcConfig
{
    int gpio_num = 3;                ///< GPIO Pin number connected to the battery divider output
    uint8_t sample_count = 16;       ///< Number of samples to average for a single reading
    uint32_t sample_delay_us = 1000; ///< Delay between samples in microseconds
    bool enable_calibration = true;  ///< Flag to enable/disable ADC calibration
};

/**
 * @brief Result of a battery measurement.
 */
struct BatteryReading
{
    uint16_t voltage_mv = 0;                    ///< Estimated battery voltage in millivolts
    uint16_t adc_mv = 0;                        ///< Measured ADC pin voltage in millivolts
    uint8_t percent = 0;                        ///< Estimated charge percentage (0-100)
    BatteryState state = BatteryState::UNKNOWN; ///< Current state classification
};

} // namespace battery_monitor
