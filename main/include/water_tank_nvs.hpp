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
    WaterTankNvs(IHalNvs &hal);
    
    WaterTankStats stats;

protected:
    esp_err_t loadAppData() override;
    esp_err_t saveAppData() override;
    void setAppDefaults() override;
};
