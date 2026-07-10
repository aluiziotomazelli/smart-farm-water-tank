// components/battery_monitor/src/battery_monitor.cpp
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "battery_monitor.hpp"

static const char* TAG = "BatteryMonitor";

namespace battery_monitor {

BatteryMonitor::BatteryMonitor(IAdcBatteryReader& adc_reader, const BatteryMonitorConfig& config)
    : adc_reader_(adc_reader)
    , config_(config)
    , initialized_(false)
{
}

esp_err_t BatteryMonitor::init()
{
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = adc_reader_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC reader: %d", err);
        return err;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized successfully");
    return ESP_OK;
}

esp_err_t BatteryMonitor::deinit()
{
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = adc_reader_.deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ADC reader: %d", err);
        return err;
    }

    initialized_ = false;
    ESP_LOGI(TAG, "Deinitialized successfully");
    return ESP_OK;
}

esp_err_t BatteryMonitor::read(BatteryReading& out)
{
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot read: component not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t adc_mv = 0;
    esp_err_t err = adc_reader_.read_adc_mv(adc_mv);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC voltage: %d", err);
        return err;
    }

    out.adc_mv = adc_mv;

    // Calculate battery voltage using voltage divider compensation
    if (config_.divider_bottom_ohms == 0) {
        ESP_LOGE(TAG, "Invalid config: divider_bottom_ohms cannot be 0");
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t calculated_voltage = static_cast<uint32_t>(adc_mv) *
                                  (config_.divider_top_ohms + config_.divider_bottom_ohms) /
                                  config_.divider_bottom_ohms;

    out.voltage_mv = static_cast<uint16_t>(calculated_voltage);

    // Calculate battery percentage (linear mapping)
    if (out.voltage_mv <= config_.empty_mv) {
        out.percent = 0;
    }
    else if (out.voltage_mv >= config_.full_mv) {
        out.percent = 100;
    }
    else {
        out.percent = static_cast<uint8_t>(
            (static_cast<uint32_t>(out.voltage_mv - config_.empty_mv) * 100) / (config_.full_mv - config_.empty_mv));
    }

    // Classify battery state
    if (out.voltage_mv <= config_.critical_mv) {
        out.state = BatteryState::CRITICAL;
    }
    else if (out.voltage_mv <= config_.low_mv) {
        out.state = BatteryState::LOW;
    }
    else if (out.voltage_mv >= config_.full_mv) {
        out.state = BatteryState::FULL;
    }
    else {
        out.state = BatteryState::NORMAL;
    }

    return ESP_OK;
}

bool BatteryMonitor::is_initialized() const
{
    return initialized_;
}

} // namespace battery_monitor
