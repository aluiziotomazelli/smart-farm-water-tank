#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_water_tank_storage.hpp"

class MockWaterTankStorage : public IWaterTankStorage {
public:
    MOCK_METHOD(esp_err_t, load, (WaterTankStats &stats), (override));
    MOCK_METHOD(esp_err_t, save, (const WaterTankStats &stats), (override));
    MOCK_METHOD(void, reset_to_defaults, (WaterTankStats &stats), (override));
};
