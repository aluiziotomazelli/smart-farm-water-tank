#include "water_tank_nvs.hpp"
#include "core_types.hpp"
#include "esp_log.h"
#include <cstring>

#include "esp_attr.h"

static const char* TAG = "WaterTankNvs";

RTC_DATA_ATTR static WaterTankStats rtc_tank_stats;
RTC_DATA_ATTR static bool rtc_tank_stats_valid = false;

WaterTankNvs::WaterTankNvs(idf_hals::INvsHAL& hal)
    : NvsCore("water_tank", hal)
{
}

esp_err_t WaterTankNvs::load_app_data()
{
    if (rtc_tank_stats_valid) {
        ESP_LOGI(TAG, "Loaded tank stats from RTC memory");
        stats = rtc_tank_stats;
        return ESP_OK;
    }

    esp_err_t err = load_struct("tank_stats", stats);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded tank stats from NVS flash");
        rtc_tank_stats = stats;
        rtc_tank_stats_valid = true;
    } else {
        ESP_LOGW(TAG, "Failed to load tank stats from NVS, using defaults");
        stats.reset();
        rtc_tank_stats = stats;
        rtc_tank_stats_valid = true;
    }
    return err;
}

esp_err_t WaterTankNvs::save_app_data(bool force_nvs)
{
    bool is_dirty = !rtc_tank_stats_valid || (stats != rtc_tank_stats);
    bool need_nvs = force_nvs || is_dirty;

    rtc_tank_stats = stats;
    rtc_tank_stats_valid = true;

    if (need_nvs) {
        ESP_LOGI(TAG, "Saved tank stats to NVS flash (dirty: %d, force: %d)", is_dirty, force_nvs);
        return save_struct("tank_stats", stats);
    }
    return ESP_OK;
}

void WaterTankNvs::set_app_defaults()
{
    ESP_LOGI(TAG, "Setting application default values");
    stats.reset();
    rtc_tank_stats = stats;
    rtc_tank_stats_valid = true;

    // Core identity defaults
    core_.node_id = farm::NodeId::WATER_TANK;
    core_.node_type = farm::NodeType::SENSOR;
    core_.power_profile = PowerProfile::DEEP_SLEEP;
}
