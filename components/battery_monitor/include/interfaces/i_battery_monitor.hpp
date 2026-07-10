// components/battery_monitor/include/interfaces/i_battery_monitor.hpp
#pragma once

#include "esp_err.h"

#include "battery_monitor_types.hpp"

namespace battery_monitor {

/**
 * @brief Interface for the battery monitor component.
 *
 * Provides functions to initialize, deinitialize, and read the state of the battery.
 */
class IBatteryMonitor {
public:
    virtual ~IBatteryMonitor() = default;

    /**
     * @brief Initializes the battery monitor component and its dependencies.
     *
     * @return ESP_OK: on success
     * @return ESP_ERR_INVALID_STATE: if already initialized
     * @return ESP_FAIL: or other esp_err_t on underlying driver failure
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Deinitializes the battery monitor component.
     *
     * @return ESP_OK: on success
     * @return ESP_ERR_INVALID_STATE: if not initialized
     */
    virtual esp_err_t deinit() = 0;

    /**
     * @brief Reads the current battery status.
     *
     * @param[out] out Structure containing the voltage, percentage, and state
     * @return ESP_OK: on success
     * @return ESP_ERR_INVALID_STATE: if component is not initialized
     * @return ESP_FAIL: or other esp_err_t on reading failure
     */
    virtual esp_err_t read(BatteryReading& out) = 0;

    /**
     * @brief Checks if the battery monitor component is initialized.
     *
     * @return true if initialized, false otherwise
     */
    virtual bool is_initialized() const = 0;
};

} // namespace battery_monitor
