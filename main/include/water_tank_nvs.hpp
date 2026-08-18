// main/include/water_tank_nvs.hpp
#pragma once

#include "app_storage.hpp"
#include "interfaces/i_persistence_backend.hpp"
#include "interfaces/i_water_tank_nvs.hpp"
#include "water_tank_stats.hpp"

/**
 * @class WaterTankNvs
 * @brief Persistent storage handler for the Water Tank application.
 */
class WaterTankNvs : public IWaterTankNvs,
                     public AppStorage<WaterTankStats, WATER_TANK_STATS_MAGIC, WATER_TANK_STATS_VERSION>
{
public:
    WaterTankNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats)
        : AppStorage<WaterTankStats, WATER_TANK_STATS_MAGIC, WATER_TANK_STATS_VERSION>(rtc_stats, nvs_stats, "WaterTankNvs")
    {
    }

    /** @copydoc IWaterTankNvs::init_app_data */
    esp_err_t init_app_data(WaterTankStats& stats, const WaterTankStats& default_stats) override
    {
        return init_app_data_impl(stats, default_stats);
    }

    /** @copydoc IWaterTankNvs::load_app_data */
    esp_err_t load_app_data(WaterTankStats& stats) override
    {
        return load_app_data_impl(stats);
    }

    /** @copydoc IWaterTankNvs::save_app_data */
    esp_err_t save_app_data(const WaterTankStats& stats, bool force_nvs_commit = false) override
    {
        return save_app_data_impl(stats, force_nvs_commit);
    }
};
