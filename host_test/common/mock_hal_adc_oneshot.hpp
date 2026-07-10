// host_test/common/mock_hal_adc_oneshot.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_hal_adc_oneshot.hpp"

namespace battery_monitor {

class MockHalAdcOneshot : public IHalAdcOneshot {
public:
    MOCK_METHOD(esp_err_t, new_unit, (const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit), (override));
    MOCK_METHOD(esp_err_t, config_channel, (adc_oneshot_unit_handle_t handle, adc_channel_t channel, const adc_oneshot_chan_cfg_t *config), (override));
    MOCK_METHOD(esp_err_t, read, (adc_oneshot_unit_handle_t handle, adc_channel_t channel, int *out_raw), (override));
    MOCK_METHOD(esp_err_t, del_unit, (adc_oneshot_unit_handle_t handle), (override));
    MOCK_METHOD(esp_err_t, io_to_channel, (int io_num, adc_unit_t *unit_id, adc_channel_t *channel), (override));
};

} // namespace battery_monitor
