#include "esp_log.h"
#include "espnow_manager.hpp"
#include "float_switch.hpp"
#include "hal_sleep.hpp"
#include "power_control.hpp"
#include "protocol_types.hpp"
#include "tank_geometry.hpp"
#include "ultrasonic_adapter.hpp"
#include "us_types.hpp"
#include "water_tank_app.hpp"
#include "water_tank_nvs.hpp"
#include "persistence_backend.hpp"
#include "secrets.hpp"
#include "ota_manager.hpp"
#include "button_ota_trigger.hpp"
#include "espnow_ota_trigger.hpp"
#include "wifi_manager.hpp"

#include "battery_monitor.hpp"
#include "adc_battery_reader.hpp"
#include "hal_adc_oneshot.hpp"
#include "hal_adc_calibration.hpp"
#include "hal_sys_rom.hpp"
#include "hal_system.hpp"
#include "hal_gpio.hpp"
#include "hal_timer.hpp"
#include "hal_nvs.hpp"
#include "hal_freertos.hpp"
#include "farm_protocol_types.hpp"
#include <cstdint>

#include "freertos/ringbuf.h"
#include "lwip/sockets.h"

static const char* TAG = "main";

static constexpr bool IS_LOGGING = true;
static constexpr bool ENTER_SLEEP = true;

// Production Configuration for XIAO-ESP32-C3 Mini Board
static constexpr gpio_num_t POWER_GPIO = GPIO_NUM_10;        // D10
static constexpr gpio_num_t US_TRIG_GPIO = GPIO_NUM_4;       // D2
static constexpr gpio_num_t US_ECHO_GPIO = GPIO_NUM_5;       // D3
static constexpr gpio_num_t FLOAT_SWITCH_GPIO = GPIO_NUM_2;  // D0 need be D0-D3 GPIO 3-5 to enable deep-sleep wake-up
static constexpr gpio_num_t BATTERY_LEVEL_GPIO = GPIO_NUM_3; // D1
static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_9;   // Boot button has no external pad

static constexpr const char* CORE_NVS_KEY = "core";
static constexpr const char* STATS_NVS_KEY = "tank_stats";

// HAL instances for sharing across components
static idf_hals::GpioHAL hal_gpio;
static idf_hals::TimerHAL hal_timer;
static idf_hals::SysRomHAL hal_sys_rom;
static idf_hals::SystemHAL hal_system;
static idf_hals::NvsHAL nvs_hal;
static idf_hals::HalFreertos hal_freertos;

// PowerControl
static power_control::PowerControl power{hal_gpio, POWER_GPIO, /*inverted_logic=*/true, /*initial_on=*/false};

// FloatSwitch config
floatswitch::Config float_switch_config = {
    .gpio = FLOAT_SWITCH_GPIO,
    .normally_open = true,
    .debounce_time_us = 50000,
    .active_level = floatswitch::ActiveLevel::LOW,
    .wakeup_on = floatswitch::WakeupCondition::NEVER};

static floatswitch::FloatSwitch float_switch{float_switch_config, hal_gpio, hal_timer, hal_sys_rom};

// BatteryMonitor
static idf_hals::HalAdcOneshot oneshot_hal;
static idf_hals::HalAdcCalibration cali_hal;

static battery_monitor::BatteryAdcConfig adc_config = {
    .gpio_num = static_cast<int>(BATTERY_LEVEL_GPIO),
    .sample_count = 16,
    .sample_delay_us = 1000,
    .enable_calibration = true};

static battery_monitor::BatteryMonitorConfig monitor_config = {
    .divider_top_ohms = 240000,
    .divider_bottom_ohms = 240000};

static battery_monitor::AdcBatteryReader adc_reader{oneshot_hal, cali_hal, hal_sys_rom, adc_config};
static battery_monitor::BatteryMonitor bat_monitor{adc_reader, monitor_config};

// Ultrasonic Sensor
ultrasonic::UsConfig us_config{
    .ping_interval_ms = 100,
    .ping_duration_us = 20,
    .timeout_us = 13000,
    .filter = ultrasonic::Filter::DOMINANT_CLUSTER,
    .min_distance_cm = SENSOR_MIN_DISTANCE_CM,
    .max_distance_cm = SENSOR_MAX_DISTANCE_CM,
    .warmup_time_ms = 0};

static ultrasonic::UsSensor
    sensor_us{hal_gpio, hal_timer, hal_sys_rom, hal_freertos, US_TRIG_GPIO, US_ECHO_GPIO, us_config};
static UltrasonicLevelSensorAdapter sensor_adapter{sensor_us};

// SleepHAL
static idf_hals::SleepHAL sleep_hw;

// Persistence and App instantiation
static RTC_DATA_ATTR CoreStorage g_rtc_core;
static RtcBackend rtc_core_backend(&g_rtc_core, sizeof(CoreStorage));
static NvsBackend nvs_core_backend{nvs_hal, CORE_NVS_KEY};
static NvsCore nvs_core{rtc_core_backend, nvs_core_backend};

static RTC_DATA_ATTR WaterTankStats g_rtc_tank;
static RtcBackend rtc_stats_backend(&g_rtc_tank, sizeof(WaterTankStats));
static NvsBackend nvs_stats_backend{nvs_hal, STATS_NVS_KEY};
static WaterTankNvs nvs_tank{rtc_stats_backend, nvs_stats_backend};

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
    .restart_on_success = false,
};
static OtaManager ota_manager(ota_deps);

// OTA triggers: boot button + espnow
static ButtonOtaTrigger btn_trigger(hal_gpio, hal_freertos, BOOT_BUTTON_GPIO, 200);
static EspNowOtaTrigger espnow_ota_trigger;

#include "udp_logger.hpp"

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing Smart Farm Water Tank...");

    // Initialize NVS partition first so components using NVS (like WiFi / Storage) can operate
    // if (nvs.init_partition() != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to initialize NVS partition!");
    // }

    // Create ESP-NOW receive queue
    QueueHandle_t app_rx_queue = hal_freertos.queue_create(30, sizeof(espnow::AppMessage));

    // Retrieve singleton references for DI
    auto& wifi = wifi_manager::WiFiManager::get_instance();
    auto& espnow = espnow::EspNowManager::instance();

    // Instantiate app with dependencies
    WaterTankApp app(
        nvs_core,
        nvs_tank,
        sensor_adapter,
        float_switch,
        espnow,
        app_rx_queue,
        power,
        sleep_hw,
        bat_monitor,
        hal_timer,
        hal_freertos,
        logic,
        wifi,
        ota_manager,
        btn_trigger,
        espnow_ota_trigger,
        hal_system);

    // Initialize application state (enable remote logging for field tests)

    if (app.init(IS_LOGGING) != ESP_OK) {
        ESP_LOGE(TAG, "Critical hardware/application initialization failure. Entering safe deep sleep for 1 minute.");
        sleep_hw.enable_timer_wakeup(1ULL * 60ULL * 1000ULL * 1000ULL);
        sleep_hw.deep_sleep_start();
        return;
    }

    // Run the main application flow
    app.run(ENTER_SLEEP);
}
