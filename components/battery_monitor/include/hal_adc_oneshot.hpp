// components/battery_monitor/include/hal_adc_oneshot.hpp
#pragma once

#include "interfaces/i_hal_adc_oneshot.hpp"

namespace battery_monitor {

/**
 * @brief Concrete implementation of IHalAdcOneshot wrapping ESP-IDF ADC Oneshot APIs.
 */
class HalAdcOneshot : public IHalAdcOneshot {
public:
    HalAdcOneshot() = default;
    ~HalAdcOneshot() override = default;

    /** @copydoc IHalAdcOneshot::new_unit */
    esp_err_t new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit) override;

    /** @copydoc IHalAdcOneshot::config_channel */
    esp_err_t config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel, const adc_oneshot_chan_cfg_t *config) override;

    /** @copydoc IHalAdcOneshot::read */
    esp_err_t read(adc_oneshot_unit_handle_t handle, adc_channel_t channel, int *out_raw) override;

    /** @copydoc IHalAdcOneshot::del_unit */
    esp_err_t del_unit(adc_oneshot_unit_handle_t handle) override;

    /** @copydoc IHalAdcOneshot::io_to_channel */
    esp_err_t io_to_channel(int io_num, adc_unit_t *unit_id, adc_channel_t *channel) override;
};

} // namespace battery_monitor
