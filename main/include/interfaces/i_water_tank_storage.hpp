#pragma once
#include "esp_err.h"
#include "water_tank_stats.hpp"

/**
 * @class IWaterTankStorage
 * @brief Interface for persisting water tank application state and statistics.
 *
 * Abstracts both NVS and RTC memory storage, allowing the logic to be tested
 * without physical non-volatile storage.
 */
class IWaterTankStorage
{
public:
    virtual ~IWaterTankStorage() = default;

    /**
     * @brief Loads the application statistics and state.
     * @param[out] stats The struct to populate with loaded data.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t load(WaterTankStats &stats) = 0;

    /**
     * @brief Persists the application statistics and state.
     * @param[in] stats The struct containing the data to save.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t save(const WaterTankStats &stats) = 0;

    /**
     * @brief Resets the storage to default values.
     * @param[out] stats The struct to reset to defaults.
     */
    virtual void reset_to_defaults(WaterTankStats &stats) = 0;
};
