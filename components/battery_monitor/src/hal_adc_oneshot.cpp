// components/battery_monitor/src/hal_adc_oneshot.cpp
#include "hal_adc_oneshot.hpp"

namespace battery_monitor {

esp_err_t HalAdcOneshot::new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit) {
    return ::adc_oneshot_new_unit(init_config, ret_unit);
}

esp_err_t HalAdcOneshot::config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel, const adc_oneshot_chan_cfg_t *config) {
    return ::adc_oneshot_config_channel(handle, channel, config);
}

esp_err_t HalAdcOneshot::read(adc_oneshot_unit_handle_t handle, adc_channel_t channel, int *out_raw) {
    return ::adc_oneshot_read(handle, channel, out_raw);
}

esp_err_t HalAdcOneshot::del_unit(adc_oneshot_unit_handle_t handle) {
    return ::adc_oneshot_del_unit(handle);
}

esp_err_t HalAdcOneshot::io_to_channel(int io_num, adc_unit_t *unit_id, adc_channel_t *channel) {
    return ::adc_oneshot_io_to_channel(io_num, unit_id, channel);
}

} // namespace battery_monitor
