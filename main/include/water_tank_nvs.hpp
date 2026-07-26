#pragma once

#include "nvs_core.hpp"
#include "interfaces/i_hal_nvs.hpp"
#include "water_tank_stats.hpp"

/**
 * @class WaterTankNvs
 * @brief Persistent storage handler for the Water Tank application.
 */
class WaterTankNvs : public NvsCore
{
public:
    WaterTankNvs(idf_hals::INvsHAL &hal);
    
    WaterTankStats stats;

protected:
    esp_err_t load_app_data() override;
    esp_err_t save_app_data(bool force_nvs = false) override;
    void set_app_defaults() override;
};
