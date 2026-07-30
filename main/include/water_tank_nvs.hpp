#pragma once

#include "nvs_core.hpp"
#include "interfaces/i_persistence_backend.hpp"
#include "interfaces/i_water_tank_nvs.hpp"
#include "water_tank_stats.hpp"

/**
 * @class WaterTankNvs
 * @brief Persistent storage handler for the Water Tank application.
 */
class WaterTankNvs : public IWaterTankNvs
{
public:
    WaterTankNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats);
    virtual ~WaterTankNvs() override = default;

    esp_err_t load_app_data(WaterTankStats& stats) override;
    esp_err_t save_app_data(const WaterTankStats& stats, bool force_nvs_commit = false) override;

private:
    IPersistenceBackend& rtc_stats_;
    IPersistenceBackend& nvs_stats_;

    esp_err_t load_raw_app_data(WaterTankStats& data_out);
    esp_err_t validate_app_data(const WaterTankStats& data);
    bool is_app_data_dirty(const WaterTankStats& new_data) const;

    /**
     * @brief Calculates the CRC of the given data.
     * @tparam T The type of the data to calculate the CRC of.
     * @param data The data to calculate the CRC of.
     * @return The CRC of the given data.
     *
     * @note: Compile-time validations:
     *          - T must be standard_layout (safe for offsetof)
     *          - T must have a crc field (not at offset 0)
     */
    template <typename T> uint32_t calculate_crc(const T& data)
    {
        static_assert(std::is_standard_layout_v<T>, "T must be standard_layout for safe offset calculation");
        static_assert(offsetof(T, crc) != 0, "T must have a non-first crc field");

        // Include from espnow_manager or define locally
        return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&data), offsetof(T, crc));
    };
};
