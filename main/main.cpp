#include "us_types.hpp"
#include "water_tank_app.hpp"
#include "esp_log.h"
#include "protocol_types.hpp"
#include "float_switch.hpp"
#include "water_tank_storage_adapter.hpp"
#include "espnow_manager.hpp"
#include "power_control.hpp"
#include "hal_nvs_core.hpp"
#include "water_tank_nvs.hpp"
#include "ultrasonic_adapter.hpp"
#include "tank_geometry.hpp"
#include "hal_sleep.hpp"

#include "wifi_manager.hpp"

static const char* TAG = "main";

// Production Configuration for XIAO-ESP32-C3 Mini Board
static constexpr gpio_num_t POWER_GPIO = GPIO_NUM_10;        // D10
static constexpr gpio_num_t US_TRIG_GPIO = GPIO_NUM_4;       // D2
static constexpr gpio_num_t US_ECHO_GPIO = GPIO_NUM_5;       // D3
static constexpr gpio_num_t FLOAT_SWITCH_GPIO = GPIO_NUM_7;  // D5
static constexpr gpio_num_t BATTERY_LEVEL_GPIO = GPIO_NUM_3; // D1

// Static allocation for production hardware components
// PowerControl
static power_control::GpioHAL gpio_hal_pc;
static power_control::PowerControl power{gpio_hal_pc, POWER_GPIO, true, false};

// FloatSwitch
static floatswitch::GpioHAL gpio_hal_fs;
static floatswitch::TimerHAL timer_hal;

floatswitch::Config float_switch_config = {
    .gpio = FLOAT_SWITCH_GPIO,
    .normally_open = true,
    .debounce_time_us = 50000,
    .active_level = floatswitch::ActiveLevel::LOW,
    .wakeup_on = floatswitch::WakeupCondition::WHEN_TANK_IS_EMPTY};

static floatswitch::FloatSwitch float_switch{float_switch_config, gpio_hal_fs, timer_hal};

// Ultrasonic Sensor
ultrasonic::UsConfig us_config{
    .ping_interval_ms = 70,
    .ping_duration_us = 20,
    .timeout_us = 25000,
    .filter = ultrasonic::Filter::DOMINANT_CLUSTER,
    .min_distance_cm = SENSOR_MIN_DISTANCE_CM,
    .max_distance_cm = SENSOR_MAX_DISTANCE_CM,
    .warmup_time_ms = 600};

static ultrasonic::UsSensor sensor_us{US_TRIG_GPIO, US_ECHO_GPIO, us_config};
static UltrasonicLevelSensorAdapter sensor_adapter{sensor_us};

// SleepHAL
static SleepHAL sleep_hw;

// NVS
static HalNvs hal_nvs;
static WaterTankNvs nvs{hal_nvs};

// StorageAdapter
static WaterTankStorageAdapter storage_adapter{nvs};

// TankGeometry
static TankGeometry geometry{SENSOR_OFFSET_CM};

// WaterTankLogic
static WaterTankLogic logic{geometry, float_switch};

// Setup Hardware
static esp_err_t setup_hardware()
{
    esp_err_t err;

    // WifiManager
    wifi_manager::WiFiManager& wifi = wifi_manager::WiFiManager::get_instance();
    if ((err = wifi.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = wifi.start()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFiManager: %s", esp_err_to_name(err));
        return err;
    }

    // FloatSwitch
    if ((err = float_switch.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize FloatSwitch: %s", esp_err_to_name(err));
        return err;
    }

    // NVS
    if ((err = nvs.init_partition()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS partition: %s", esp_err_to_name(err));
        return err;
    }

    // EspNowManager
    espnow::EspNowConfig config;
    config.node_id = static_cast<espnow::NodeId>(FarmNodeId::WATER_TANK);
    config.node_type = static_cast<espnow::NodeType>(FarmNodeType::SENSOR);
    config.app_rx_queue = xQueueCreate(30, sizeof(espnow::AppMessage));
    config.wifi_channel = 1;

    espnow::EspNowManager& espnow = espnow::EspNowManager::instance();
    if ((err = espnow.init(config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize EspNowManager: %s", esp_err_to_name(err));
        return err;
    }

    // PowerControl
    if ((err = power.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PowerControl: %s", esp_err_to_name(err));
        return err;
    }
    power.turn_on();

    // UsSensor
    if ((err = sensor_us.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UsSensor: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing Smart Farm Water Tank...");

    if (setup_hardware() != ESP_OK) {
        ESP_LOGE(TAG, "Critical hardware initialization failure. Entering safe deep sleep for 5 minutes.");
        sleep_hw.enable_timer_wakeup(5ULL * 60ULL * 1000ULL * 1000ULL);
        sleep_hw.deep_sleep_start();
        return;
    }

    // Retrieve singleton references for DI
    auto& wifi = wifi_manager::WiFiManager::get_instance();
    auto& espnow = espnow::EspNowManager::instance();

    // Instantiate app with dependencies
    WaterTankApp app(sensor_adapter, float_switch, storage_adapter, espnow, wifi, power, sleep_hw, logic);

    // Run the main application flow
    app.run();
}
