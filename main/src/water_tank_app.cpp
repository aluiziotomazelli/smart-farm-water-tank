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
    QueueHandle_t rx_queue,
    power_control::IPowerControl& power,
    idf_hals::ISleepHAL& sleep,
    battery_monitor::IBatteryMonitor& battery_monitor,
    idf_hals::ITimerHAL& sys_timer,
    idf_hals::IHalFreertos& rtos,
    WaterTankLogic& logic,
    EspNowOtaTrigger& espnow_ota_trigger,
    OtaController& ota_controller)
    : sensor_(sensor)
    , float_switch_(float_switch)
    , storage_(storage)
    , comm_(comm)
    , rx_queue_(rx_queue)
    , power_(power)
    , sleep_(sleep)
    , battery_monitor_(battery_monitor)
    , sys_timer_(sys_timer)
    , rtos_(rtos)
    , logic_(logic)
    , espnow_ota_trigger_(espnow_ota_trigger)
    , ota_controller_(ota_controller)
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

    // 5. Read battery status
    if (battery_monitor_.init() == ESP_OK) {
        battery_monitor::BatteryReading bat_reading;
        if (battery_monitor_.read(bat_reading) == ESP_OK) {
            logic_.process_battery(bat_reading.voltage_mv, stats_);
            ESP_LOGI(TAG, "Battery: %d mV (%d%%), state: %d", 
                     stats_.last_battery_mv, stats_.last_battery_percent, static_cast<int>(stats_.last_battery_state));
        }
        battery_monitor_.deinit();
    }

    // 6. Transmit data to Hub
    send_report();

    // 7. Listen for incoming commands (e.g. START_OTA) before sleeping
    listen_for_commands(LISTEN_WINDOW_MS);

    // 8. Calculate sleep and enter deep sleep
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
    report.battery_mv = stats_.last_battery_mv;
    report.battery_percent = stats_.last_battery_percent;
    report.battery_state = stats_.last_battery_state;
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
    if (ota_controller_.is_busy()) {
        ESP_LOGW(TAG, "OTA in progress, waiting for completion before sleeping...");
        while (ota_controller_.is_busy()) {
            rtos_.task_delay(pdMS_TO_TICKS(1000));
        }
        ESP_LOGI(TAG, "OTA finished (failed/cancelled). Proceeding to deep sleep.");
    }

    ESP_LOGI(TAG, "Entering deep sleep for %llu s", sleep_time_us / 1000000);

    sleep_.disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    stats_.gpio_wakeup_enabled = false;
    if (float_switch_.should_enable_wakeup()) {
        int gpio_num;
        bool wake_high;
        if (float_switch_.get_wakeup_config(gpio_num, wake_high) == ESP_OK) {
            uint64_t pin_mask = 1ULL << gpio_num;
            esp_deepsleep_gpio_wake_up_mode_t mode = wake_high ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW;
            if (sleep_.deep_sleep_enable_gpio_wakeup(pin_mask, mode) == ESP_OK) {
                stats_.gpio_wakeup_enabled = true;
                ESP_LOGI(TAG, "GPIO wakeup enabled on pin %d (wake_on_high=%d)", gpio_num, wake_high);
            }
        }
    }

    if (sleep_time_us > 0) {
        sleep_.enable_timer_wakeup(sleep_time_us);
    }

    sleep_.deep_sleep_start();
}

void WaterTankApp::listen_for_commands(uint32_t timeout_ms)
{
    if (!rx_queue_) {
        rtos_.task_delay(pdMS_TO_TICKS(timeout_ms));
        return;
    }

    int64_t deadline_ms = (sys_timer_.get_time_us() / 1000) + timeout_ms;
    espnow::AppMessage msg;

    while ((sys_timer_.get_time_us() / 1000) < deadline_ms) {
        int64_t remaining = deadline_ms - (sys_timer_.get_time_us() / 1000);
        if (remaining <= 0) break;
        
        if (rtos_.queue_receive(rx_queue_, &msg, pdMS_TO_TICKS(remaining)) == pdPASS) {
            if (msg.msg_type == espnow::MessageType::COMMAND) {
                auto cmd = static_cast<espnow::CommandType>(msg.payload_type);
                if (cmd == espnow::CommandType::START_OTA) {
                    ESP_LOGW(TAG, "Received START_OTA command from Hub - triggering OTA");
                    espnow_ota_trigger_.notify();
                }
            }
        }
    }
}

