// components/battery_monitor/include/hal_adc_calibration.hpp
#pragma once

#include "interfaces/i_hal_adc_calibration.hpp"

namespace battery_monitor {

/**
 * @brief Concrete implementation of IHalAdcCalibration wrapping ESP-IDF ADC Calibration APIs.
 */
class HalAdcCalibration : public IHalAdcCalibration {
public:
    HalAdcCalibration() = default;
    ~HalAdcCalibration() override = default;

    /** @copydoc IHalAdcCalibration::create_scheme_curve_fitting */
    esp_err_t create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *ret_handle) override;

    /** @copydoc IHalAdcCalibration::delete_scheme_curve_fitting */
    esp_err_t delete_scheme_curve_fitting(adc_cali_handle_t handle) override;

    /** @copydoc IHalAdcCalibration::raw_to_voltage */
    esp_err_t raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage) override;
};

} // namespace battery_monitor
