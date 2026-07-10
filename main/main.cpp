#include "esp_log.h"
#include "espnow_manager.hpp"
#include "float_switch.hpp"
#include "hal_nvs_core.hpp"
#include "hal_sleep.hpp"
#include "power_control.hpp"
#include "protocol_types.hpp"
#include "tank_geometry.hpp"
#include "ultrasonic_adapter.hpp"
#include "us_types.hpp"
#include "water_tank_app.hpp"
#include "water_tank_nvs.hpp"
#include "water_tank_storage_adapter.hpp"
#include "secrets.hpp"
#include "ota_manager.hpp"
#include "boot_button_ota_trigger.hpp"
#include "ota_controller.hpp"

#include "wifi_manager.hpp"

#include "battery_monitor.hpp"
#include "adc_battery_reader.hpp"
#include "hal_adc_oneshot.hpp"
#include "hal_adc_calibration.hpp"
#include "bm_hal_timer.hpp"

static const char* TAG = "main";

// Production Configuration for XIAO-ESP32-C3 Mini Board
static constexpr gpio_num_t POWER_GPIO = GPIO_NUM_10;        // D10
static constexpr gpio_num_t US_TRIG_GPIO = GPIO_NUM_4;       // D2
static constexpr gpio_num_t US_ECHO_GPIO = GPIO_NUM_5;       // D3
static constexpr gpio_num_t FLOAT_SWITCH_GPIO = GPIO_NUM_7;  // D5
static constexpr gpio_num_t BATTERY_LEVEL_GPIO = GPIO_NUM_3; // D1
static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_9;   // Boot button has no external pad

// Static allocation for production hardware components
// PowerControl
static power_control::GpioHAL gpio_hal_pc;
static power_control::PowerControl power{gpio_hal_pc, POWER_GPIO, /*inverted_logic=*/true, /*initial_on=*/false};

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

// BatteryMonitor
static battery_monitor::HalAdcOneshot oneshot_hal;
static battery_monitor::HalAdcCalibration cali_hal;
static battery_monitor::BmHalTimer timer_hal_bm;

static battery_monitor::BatteryAdcConfig adc_config = {
    .gpio_num = static_cast<int>(BATTERY_LEVEL_GPIO),
    .sample_count = 16,
    .sample_delay_us = 1000,
    .enable_calibration = true
};

static battery_monitor::BatteryMonitorConfig monitor_config = {
    .divider_top_ohms = 240000,
    .divider_bottom_ohms = 240000
};

static battery_monitor::AdcBatteryReader adc_reader{oneshot_hal, cali_hal, timer_hal_bm, adc_config};
static battery_monitor::BatteryMonitor bat_monitor{adc_reader, monitor_config};

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

// OtaManager — HAL implementations
static HttpClient http_client;
static ManifestParser manifest_parser;
static OtaSession ota_session;
static System ota_system;
static TaskScheduler task_scheduler;
static RollbackManager rollback_manager;
static OtaDependencies ota_deps = {
    .http_client = http_client,
    .manifest_parser = manifest_parser,
    .ota_session = ota_session,
    .system = ota_system,
    .task_scheduler = task_scheduler,
    .rollback_manager = rollback_manager,
};

static OtaConfig ota_config{
    .device_type = "water_tank",
    .manifest_url = SERVER_URL,
    .task_stack_size = 8192,
    .task_priority = 5,
    .transport = {.manifest_timeout_ms = 30000, .firmware_timeout_ms = 30000},
    .security = {.allow_http_during_development = true},
    .allow_same_version = false,
    .restart_on_success = true,
};
static OtaManager ota_manager(ota_deps);

// OTA trigger: button BOOT (GPIO 9 no XIAO-ESP32-C3)
static BootButtonOtaTrigger ota_trigger(BOOT_BUTTON_GPIO);

// OTA Controller config
static OtaControllerConfig ota_ctrl_config{
    .wifi_connect_timeout_ms = 30000,
    .trigger_poll_interval_ms = 100,
    .trigger_timeout_ms = 5000, // window of 10s to press the button
};

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
    // Before wifi.start() — define the credentials for OTA
    if ((err = wifi.add_credentials(WIFI_SSID, WIFI_PASS)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi credentials: %s", esp_err_to_name(err));
        // not fatal: credentials may already be in NVS
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

    // OTA Manager
    if (!ota_manager.init(ota_config)) {
        ESP_LOGE(TAG, "Failed to initialize OTA Manager");
        return ESP_FAIL;
    }

    // OTA Trigger
    if ((err = ota_trigger.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize OTA Trigger: %s", esp_err_to_name(err));
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
    OtaController ota_controller(ota_manager, wifi, ota_trigger, ota_ctrl_config);

    // Verify rollback state on boot
    ota_controller.handle_pending_verify();

    // OTA window: press button to trigger update
    if (ota_controller.wait_and_run() != ESP_OK) {
        ESP_LOGW(TAG, "OTA window expired. Proceeding with normal boot.");
    }

    // Instantiate app with dependencies
    WaterTankApp app(sensor_adapter, float_switch, storage_adapter, espnow, power, sleep_hw, bat_monitor, logic);

    // Run the main application flow
    app.run();
}
