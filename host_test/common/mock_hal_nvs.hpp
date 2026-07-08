// host_test_common/mock_hal_nvs.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_hal_nvs.hpp"

/**
 * @class MockHalNvs
 * @brief Google Mock implementation of IHalNvs interface.
 */
class MockHalNvs : public IHalNvs
{
public:
    MOCK_METHOD(esp_err_t, hal_nvs_flash_init, (), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_flash_erase, (), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_erase_all, (nvs_handle_t), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_open, (const char*, nvs_open_mode_t, nvs_handle_t*), (override));
    MOCK_METHOD(void, hal_nvs_close, (nvs_handle_t), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_set_blob, (nvs_handle_t, const char*, const void*, size_t), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_get_blob, (nvs_handle_t, const char*, void*, size_t*), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_commit, (nvs_handle_t), (override));
};
