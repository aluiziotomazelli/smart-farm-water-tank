// components/battery_monitor/include/adc_battery_reader.hpp
#pragma once

#include "esp_err.h"

#include "interfaces/i_adc_battery_reader.hpp"
#include "interfaces/i_hal_adc_oneshot.hpp"
#include "interfaces/i_hal_adc_calibration.hpp"
#include "interfaces/i_bm_hal_timer.hpp"
#include "battery_monitor_types.hpp"

namespace battery_monitor {

/**
 * @brief Concrete implementation of IAdcBatteryReader.
 *
 * Implements ADC initialization, deinitialization, sampling (with delay and averaging),
 * and calibration handling using injected HAL abstractions.
 */
class AdcBatteryReader : public IAdcBatteryReader {
public:
    /**
     * @brief Construct a new Adc Battery Reader object.
     *
     * @param oneshot_hal Reference to the ADC oneshot HAL.
     * @param cali_hal Reference to the ADC calibration HAL.
     * @param timer_hal Reference to the timer/delay HAL.
     * @param config Configuration for ADC pin and sampling.
     */
    AdcBatteryReader(IHalAdcOneshot& oneshot_hal, IHalAdcCalibration& cali_hal, IBmHalTimer& timer_hal, const BatteryAdcConfig& config);

    ~AdcBatteryReader() override = default;

    /** @copydoc IAdcBatteryReader::init */
    esp_err_t init() override;

    /** @copydoc IAdcBatteryReader::deinit */
    esp_err_t deinit() override;

    /** @copydoc IAdcBatteryReader::read_adc_mv */
    esp_err_t read_adc_mv(uint16_t& out_mv) override;

    /** @copydoc IAdcBatteryReader::is_initialized */
    bool is_initialized() const override;

private:
    IHalAdcOneshot& oneshot_hal_;
    IHalAdcCalibration& cali_hal_;
    IBmHalTimer& timer_hal_;
    BatteryAdcConfig config_;
    bool initialized_;
    adc_oneshot_unit_handle_t adc_handle_;
    adc_cali_handle_t cali_handle_;
    bool cali_supported_;
    adc_channel_t adc_channel_;
    adc_unit_t adc_unit_;
};

} // namespace battery_monitor
