// components/battery_monitor/include/battery_monitor.hpp
#pragma once

#include "esp_err.h"

#include "i_battery_monitor.hpp"
#include "i_adc_battery_reader.hpp"
#include "battery_monitor_types.hpp"

namespace battery_monitor {

/**
 * @brief Concrete implementation of IBatteryMonitor.
 *
 * This class is responsible for calculating battery metrics (voltage, percent, state)
 * from the raw voltage measured at the ADC node, using dependency injection for the ADC reader.
 */
class BatteryMonitor : public IBatteryMonitor {
public:
    /**
     * @brief Construct a new Battery Monitor object.
     *
     * @param adc_reader Reference to the ADC reader dependency.
     * @param config Battery threshold and voltage divider configuration.
     */
    BatteryMonitor(IAdcBatteryReader& adc_reader, const BatteryMonitorConfig& config);

    ~BatteryMonitor() override = default;

    /** @copydoc IBatteryMonitor::init */
    esp_err_t init() override;

    /** @copydoc IBatteryMonitor::deinit */
    esp_err_t deinit() override;

    /** @copydoc IBatteryMonitor::read */
    esp_err_t read(BatteryReading& out) override;

    /** @copydoc IBatteryMonitor::is_initialized */
    bool is_initialized() const override;

private:
    IAdcBatteryReader& adc_reader_;
    BatteryMonitorConfig config_;
    bool initialized_;
};

} // namespace battery_monitor
