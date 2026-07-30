#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_water_tank_nvs.hpp"

class MockWaterTankStorage : public IWaterTankNvs {
public:
    MOCK_METHOD(esp_err_t, load_app_data, (WaterTankStats &stats), (override));
    MOCK_METHOD(esp_err_t, save_app_data, (const WaterTankStats &stats, bool force_nvs_commit), (override));
};
