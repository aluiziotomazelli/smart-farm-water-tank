#include "water_tank_nvs.hpp"
#include "core_types.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "WaterTankNvs";

WaterTankNvs::WaterTankNvs(idf_hals::INvsHAL& hal)
    : NvsCore("water_tank", hal)
{
}

esp_err_t WaterTankNvs::load_app_data()
{
    return load_struct("tank_stats", stats);
}

esp_err_t WaterTankNvs::save_app_data(bool force_nvs)
{
    return save_struct("tank_stats", stats);
}

void WaterTankNvs::set_app_defaults()
{
    ESP_LOGI(TAG, "Setting application default values");
    stats.reset();

    // Core identity defaults
    core_.node_id = farm::NodeId::WATER_TANK;
    core_.node_type = farm::NodeType::SENSOR;
    core_.power_profile = PowerProfile::DEEP_SLEEP;
}
