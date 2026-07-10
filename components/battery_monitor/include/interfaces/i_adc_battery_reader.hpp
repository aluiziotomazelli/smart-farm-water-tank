// components/battery_monitor/include/interfaces/i_adc_battery_reader.hpp
#pragma once

#include "esp_err.h"

namespace battery_monitor {

/**
 * @brief Interface for the ADC battery reader component.
 *
 * Responsible for configuring the ADC, sampling the raw value,
 * applying calibration if available, and returning the voltage in mV at the ADC pin.
 */
class IAdcBatteryReader {
public:
    virtual ~IAdcBatteryReader() = default;

    /**
     * @brief Initializes the ADC reader and calibration schemes.
     *
     * @return ESP_OK: on success
     * @return ESP_ERR_INVALID_STATE: if already initialized
     * @return ESP_FAIL: or other esp_err_t on hardware driver configuration failure
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Deinitializes the ADC reader, freeing handles and calibration schemes.
     *
     * @return ESP_OK: on success
     * @return ESP_ERR_INVALID_STATE: if not initialized
     */
    virtual esp_err_t deinit() = 0;

    /**
     * @brief Reads the average voltage at the ADC pin in millivolts.
     *
     * @param[out] out_mv The measured voltage at the ADC pin in mV
     * @return ESP_OK: on success
     * @return ESP_ERR_INVALID_STATE: if not initialized
     * @return ESP_FAIL: or other esp_err_t on measurement failure
     */
    virtual esp_err_t read_adc_mv(uint16_t& out_mv) = 0;

    /**
     * @brief Checks if the ADC reader is initialized.
     *
     * @return true if initialized, false otherwise
     */
    virtual bool is_initialized() const = 0;
};

} // namespace battery_monitor
