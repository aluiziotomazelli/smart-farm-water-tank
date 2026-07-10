// components/battery_monitor/src/adc_battery_reader.cpp
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "adc_battery_reader.hpp"

static const char *TAG = "AdcBatteryReader";

namespace battery_monitor {

AdcBatteryReader::AdcBatteryReader(IHalAdcOneshot& oneshot_hal, IHalAdcCalibration& cali_hal, IBmHalTimer& timer_hal, const BatteryAdcConfig& config)
    : oneshot_hal_(oneshot_hal)
    , cali_hal_(cali_hal)
    , timer_hal_(timer_hal)
    , config_(config)
    , initialized_(false)
    , adc_handle_(nullptr)
    , cali_handle_(nullptr)
    , cali_supported_(false)
    , adc_channel_(ADC_CHANNEL_0)
    , adc_unit_(ADC_UNIT_1) {}

esp_err_t AdcBatteryReader::init() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Resolve GPIO pin to ADC unit and channel
    esp_err_t err = oneshot_hal_.io_to_channel(config_.gpio_num, &adc_unit_, &adc_channel_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resolve GPIO %d to ADC unit/channel: %d", config_.gpio_num, err);
        return err;
    }

    // Create oneshot unit handle
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = adc_unit_,
        .clk_src = static_cast<adc_oneshot_clk_src_t>(0), // Default clock source
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = oneshot_hal_.new_unit(&init_config, &adc_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC oneshot unit: %d", err);
        return err;
    }

    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,        // Set 12 dB attenuation (approx. 0-2500 mV range)
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Default bitwidth (usually 12-bit)
    };
    err = oneshot_hal_.config_channel(adc_handle_, adc_channel_, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %d", err);
        oneshot_hal_.del_unit(adc_handle_);
        adc_handle_ = nullptr;
        return err;
    }

    // Initialize calibration if enabled
    cali_supported_ = false;
    cali_handle_ = nullptr;
    if (config_.enable_calibration) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = adc_unit_,
            .chan = adc_channel_,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        err = cali_hal_.create_scheme_curve_fitting(&cali_config, &cali_handle_);
        if (err == ESP_OK) {
            cali_supported_ = true;
            ESP_LOGI(TAG, "ADC calibration initialized successfully");
        } else {
            ESP_LOGW(TAG, "ADC calibration not supported or failed: %d. Falling back to raw readings.", err);
        }
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized successfully on GPIO %d (Unit %d, Channel %d)", config_.gpio_num, adc_unit_, adc_channel_);
    return ESP_OK;
}

esp_err_t AdcBatteryReader::deinit() {
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Delete calibration handle if created
    if (cali_supported_ && cali_handle_ != nullptr) {
        esp_err_t err = cali_hal_.delete_scheme_curve_fitting(cali_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete ADC calibration: %d", err);
        }
        cali_handle_ = nullptr;
        cali_supported_ = false;
    }

    // Delete oneshot unit handle
    if (adc_handle_ != nullptr) {
        esp_err_t err = oneshot_hal_.del_unit(adc_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete ADC oneshot unit: %d", err);
        }
        adc_handle_ = nullptr;
    }

    initialized_ = false;
    ESP_LOGI(TAG, "Deinitialized successfully");
    return ESP_OK;
}

esp_err_t AdcBatteryReader::read_adc_mv(uint16_t& out_mv) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot read: component not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t sample_count = config_.sample_count > 0 ? config_.sample_count : 1;
    uint32_t raw_sum = 0;

    for (uint8_t i = 0; i < sample_count; ++i) {
        int raw_val = 0;
        esp_err_t err = oneshot_hal_.read(adc_handle_, adc_channel_, &raw_val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ADC raw value at sample %d: %d", i, err);
            return err;
        }
        raw_sum += raw_val;

        // Apply delay between samples (except for the last sample)
        if (config_.sample_delay_us > 0 && i < (sample_count - 1)) {
            timer_hal_.delay_us(config_.sample_delay_us);
        }
    }

    int avg_raw = static_cast<int>(raw_sum / sample_count);

    if (cali_supported_ && cali_handle_ != nullptr) {
        int voltage_mv = 0;
        esp_err_t err = cali_hal_.raw_to_voltage(cali_handle_, avg_raw, &voltage_mv);
        if (err == ESP_OK) {
            out_mv = static_cast<uint16_t>(voltage_mv);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Calibration conversion failed: %d. Falling back to raw conversion.", err);
    }

    // Fallback: Linear mapping.
    // ESP32-C3 ADC is 12-bit (0-4095).
    // With 12 dB attenuation, the input range is approx 0-2500 mV.
    uint32_t calculated_mv = (static_cast<uint32_t>(avg_raw) * 2500) / 4095;
    out_mv = static_cast<uint16_t>(calculated_mv);

    return ESP_OK;
}

bool AdcBatteryReader::is_initialized() const {
    return initialized_;
}

} // namespace battery_monitor
