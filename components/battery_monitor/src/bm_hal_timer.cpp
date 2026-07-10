// components/battery_monitor/src/bm_hal_timer.cpp
#include "bm_hal_timer.hpp"

#include "esp_timer.h"
#include "esp_rom_sys.h"

namespace battery_monitor {

void BmHalTimer::delay_us(uint32_t us) const {
    esp_rom_delay_us(us);
}

int64_t BmHalTimer::get_time_us() const {
    return esp_timer_get_time();
}

} // namespace battery_monitor
