// components/battery_monitor/src/hal_adc_calibration.cpp
#include "hal_adc_calibration.hpp"

namespace battery_monitor {

esp_err_t HalAdcCalibration::create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *ret_handle) {
    return ::adc_cali_create_scheme_curve_fitting(config, ret_handle);
}

esp_err_t HalAdcCalibration::delete_scheme_curve_fitting(adc_cali_handle_t handle) {
    return ::adc_cali_delete_scheme_curve_fitting(handle);
}

esp_err_t HalAdcCalibration::raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage) {
    return ::adc_cali_raw_to_voltage(handle, raw, voltage);
}

} // namespace battery_monitor
