// components/battery_monitor/include/interfaces/i_hal_adc_calibration.hpp
#pragma once

#include "esp_err.h"
#include "i_hal_adc_oneshot.hpp"

#if defined(CONFIG_IDF_TARGET_LINUX) || defined(__linux__)
// Fake definitions for host environment
typedef void* adc_cali_handle_t;

typedef struct {
    adc_unit_t unit_id;
    adc_channel_t chan;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_cali_curve_fitting_config_t;

#else
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#endif

namespace battery_monitor {

/**
 * @brief Interface for the ESP-IDF ADC calibration HAL wrapper.
 *
 * This provides a 1:1 wrapper interface for interacting with the ADC calibration APIs,
 * allowing isolation for host-based unit testing.
 */
class IHalAdcCalibration {
public:
    virtual ~IHalAdcCalibration() = default;

    /** @copydoc adc_cali_create_scheme_curve_fitting */
    virtual esp_err_t create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *ret_handle) = 0;

    /** @copydoc adc_cali_delete_scheme_curve_fitting */
    virtual esp_err_t delete_scheme_curve_fitting(adc_cali_handle_t handle) = 0;

    /** @copydoc adc_cali_raw_to_voltage */
    virtual esp_err_t raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage) = 0;
};

} // namespace battery_monitor
