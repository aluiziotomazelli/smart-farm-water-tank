// components/battery_monitor/include/interfaces/i_hal_adc_oneshot.hpp
#pragma once

#include "esp_err.h"

#if defined(CONFIG_IDF_TARGET_LINUX) || defined(__linux__)
// Fake definitions for host environment
typedef void* adc_oneshot_unit_handle_t;

typedef enum {
    ADC_UNIT_1 = 1,
    ADC_UNIT_2 = 2,
} adc_unit_t;

typedef enum {
    ADC_CHANNEL_0 = 0,
    ADC_CHANNEL_1 = 1,
    ADC_CHANNEL_2 = 2,
    ADC_CHANNEL_3 = 3,
    ADC_CHANNEL_4 = 4,
} adc_channel_t;

typedef enum {
    ADC_ATTEN_DB_12 = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_DEFAULT = 0,
} adc_bitwidth_t;

typedef enum {
    ADC_ULP_MODE_DISABLE = 0,
} adc_ulp_mode_t;

typedef int adc_oneshot_clk_src_t;

typedef struct {
    adc_unit_t unit_id;
    adc_oneshot_clk_src_t clk_src;
    adc_ulp_mode_t ulp_mode;
} adc_oneshot_unit_init_cfg_t;

typedef struct {
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_oneshot_chan_cfg_t;

#else
#include "esp_adc/adc_oneshot.h"
#endif

namespace battery_monitor {

/**
 * @brief Interface for the ESP-IDF ADC oneshot HAL wrapper.
 *
 * This provides a 1:1 wrapper interface for interacting with the ADC oneshot mode APIs,
 * allowing isolation for host-based unit testing.
 */
class IHalAdcOneshot {
public:
    virtual ~IHalAdcOneshot() = default;

    /** @copydoc adc_oneshot_new_unit */
    virtual esp_err_t new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit) = 0;

    /** @copydoc adc_oneshot_config_channel */
    virtual esp_err_t config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel, const adc_oneshot_chan_cfg_t *config) = 0;

    /** @copydoc adc_oneshot_read */
    virtual esp_err_t read(adc_oneshot_unit_handle_t handle, adc_channel_t channel, int *out_raw) = 0;

    /** @copydoc adc_oneshot_del_unit */
    virtual esp_err_t del_unit(adc_oneshot_unit_handle_t handle) = 0;

    /** @copydoc adc_oneshot_io_to_channel */
    virtual esp_err_t io_to_channel(int io_num, adc_unit_t *unit_id, adc_channel_t *channel) = 0;
};

} // namespace battery_monitor
