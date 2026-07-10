// components/battery_monitor/include/interfaces/i_bm_hal_timer.hpp
#pragma once

#include <cstdint>

namespace battery_monitor {

/**
 * @brief Interface for system time and delay services.
 *
 * Provides functions to query system time and perform microsecond delays.
 */
class IBmHalTimer {
public:
    virtual ~IBmHalTimer() = default;

    /** @copydoc esp_rom_delay_us */
    virtual void delay_us(uint32_t us) const = 0;

    /** @copydoc esp_timer_get_time */
    virtual int64_t get_time_us() const = 0;
};

} // namespace battery_monitor
