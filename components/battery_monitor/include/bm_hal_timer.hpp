// components/battery_monitor/include/bm_hal_timer.hpp
#pragma once

#include "interfaces/i_bm_hal_timer.hpp"

namespace battery_monitor {

/**
 * @brief Concrete implementation of IBmHalTimer wrapping ESP-IDF timer functions.
 */
class BmHalTimer : public IBmHalTimer {
public:
    BmHalTimer() = default;
    ~BmHalTimer() override = default;

    /** @copydoc IBmHalTimer::delay_us */
    void delay_us(uint32_t us) const override;

    /** @copydoc IBmHalTimer::get_time_us */
    int64_t get_time_us() const override;
};

} // namespace battery_monitor
