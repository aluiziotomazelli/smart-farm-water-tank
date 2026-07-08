#include "water_tank_app.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "farm_protocol_types.hpp"

static const char* TAG = "WaterTankApp";

WaterTankApp::WaterTankApp(
    ILevelSensor& sensor,
    floatswitch::IFloatSwitch& float_switch,
    IWaterTankStorage& storage,
    espnow::IEspNowManager& comm,
    wifi_manager::IWiFiManager& wifi,
    power_control::IPowerControl& power,
    ISleepHAL& sleep,
    WaterTankLogic& logic)
    : sensor_(sensor)
    , float_switch_(float_switch)
    , storage_(storage)
    , comm_(comm)
    , wifi_(wifi)
    , power_(power)
    , sleep_(sleep)
    , logic_(logic)
{
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Starting application flow");

    // 1. Load state and statistics from persistent storage
    if (storage_.load(stats_) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load storage, using defaults");
        storage_.reset_to_defaults(stats_);
    }

    // 2. Perform sensor reading
    ultrasonic::Reading reading = sensor_.read_level();
    ESP_LOGI(TAG, "Reading raw: %.1f cm (Status: %d)", reading.cm, static_cast<int>(reading.result));

    // Turn off sensor power as soon as we have the reading
    power_.turn_off();

    // 3. Process logic (Brain)
    logic_.process_reading(reading, stats_);
    logic_.update_operation_mode(stats_);

    ESP_LOGI(
        TAG,
        "Result: %d, Distance: %.1f cm, Level: %d permille, Mode: %s",
        static_cast<int>(stats_.last_result),
        stats_.last_distance_cm,
        stats_.level_permille,
        stats_.backup_mode_active ? "BACKUP" : "NORMAL");

    // 4. Save updated state
    storage_.save(stats_);

    // 5. Transmit data to Hub
    send_report();

    // 6. Calculate sleep and enter deep sleep
    uint64_t sleep_time_us = logic_.calculate_sleep_time_us(stats_);
    enter_deep_sleep(sleep_time_us);
}

// =====================================================================
// PRIVATE METHODS
// =====================================================================

esp_err_t WaterTankApp::send_report()
{
    WaterLevelReport report = {};

    report.level_permille = stats_.level_permille;
    report.distance_cm = stats_.last_distance_cm;
    report.battery_mv = 0;
    report.status = map_status(stats_.last_result);

    report.float_switch_is_full = float_switch_.is_tank_full();
    report.backup_mode_active = stats_.backup_mode_active;

    ESP_LOGI(TAG, "Sending report: %d permille", report.level_permille);

    esp_err_t err = comm_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(FarmPayloadType::WATER_LEVEL_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send report: %s", esp_err_to_name(err));
    }
    return err;
}

SensorStatus WaterTankApp::map_status(ultrasonic::UsResult result)
{
    switch (result) {
    case ultrasonic::UsResult::OK:
        return SensorStatus::OK;
    case ultrasonic::UsResult::WEAK_SIGNAL:
        return SensorStatus::WARNING_LOW_SIGNAL;
    case ultrasonic::UsResult::TIMEOUT:
        return SensorStatus::ERROR_TIMEOUT;
    case ultrasonic::UsResult::OUT_OF_RANGE:
        return SensorStatus::ERROR_OUT_OF_RANGE;
    case ultrasonic::UsResult::HIGH_VARIANCE:
    case ultrasonic::UsResult::INSUFFICIENT_SAMPLES:
        return SensorStatus::ERROR_UNSTABLE;
    case ultrasonic::UsResult::ECHO_STUCK:
    case ultrasonic::UsResult::HW_FAULT:
        return SensorStatus::ERROR_HARDWARE;
    default:
        return SensorStatus::UNKNOWN;
    }
}

void WaterTankApp::enter_deep_sleep(uint64_t sleep_time_us)
{
    ESP_LOGI(TAG, "Entering deep sleep for %llu us", sleep_time_us);

    sleep_.disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (sleep_time_us > 0) {
        sleep_.enable_timer_wakeup(sleep_time_us);
    }

    sleep_.deep_sleep_start();
}
