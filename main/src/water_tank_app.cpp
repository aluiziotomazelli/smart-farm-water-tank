#include "water_tank_app.hpp"
#include "core_types.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "espnow_ota_trigger.hpp"
#include "farm_protocol_types.hpp"
#include "freertos/projdefs.h"

#include "i_hal_nvs.hpp"

static constexpr uint32_t RUN_LOOP_DELAY_MS = 10000;
static constexpr uint32_t SENSOR_WARMUP_MS = 600;

static const char* TAG = "WaterTankApp";

WaterTankApp::WaterTankApp(
    INvsCore& core_storage,
    IWaterTankNvs& tank_storage,
    ILevelSensor& sensor,
    floatswitch::IFloatSwitch& float_switch,
    espnow::IEspNowManager& comm,
    QueueHandle_t rx_queue,
    power_control::IPowerControl& power,
    idf_hals::ISleepHAL& sleep,
    battery_monitor::IBatteryMonitor& battery_monitor,
    idf_hals::ITimerHAL& sys_timer,
    idf_hals::IHalFreertos& rtos,
    WaterTankLogic& logic,
    wifi_manager::IWiFiManager& wifi,
    IOtaManager& ota_manager,
    IOtaTrigger& btn_trigger,
    IOtaTrigger& espnow_trigger,
    idf_hals::ISystemHAL& system_hal)
    : core_storage_(core_storage)
    , tank_storage_(tank_storage)
    , sensor_(sensor)
    , float_switch_(float_switch)
    , comm_(comm)
    , rx_queue_(rx_queue)
    , power_(power)
    , sleep_(sleep)
    , battery_monitor_(battery_monitor)
    , sys_timer_(sys_timer)
    , rtos_(rtos)
    , logic_(logic)
    , wifi_(wifi)
    , ota_manager_(ota_manager)
    , btn_trigger_(btn_trigger)
    , espnow_trigger_(espnow_trigger)
    , system_hal_(system_hal)
{
}

void WaterTankApp::on_ota_triggered(OtaTriggerSource source)
{
    ESP_LOGI(TAG, "OTA triggered from source: %d", static_cast<int>(source));
    ota_triggered_ = true;
}

#include "udp_logger.hpp"
#include "secrets.hpp"

esp_err_t WaterTankApp::init(bool is_logging)
{
    esp_err_t err;

    // 1. WifiManager initialization
    if ((err = init_wifi()) != ESP_OK) {
        return err;
    }

    // 2. FloatSwitch initialization
    if ((err = float_switch_.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize FloatSwitch: %s", esp_err_to_name(err));
        return err;
    }

    // 3. Storage / NVS Data load
    if ((err = init_core_storage()) != ESP_OK) {
        return err;
    }
    if ((err = init_tank_storage()) != ESP_OK) {
        return err;
    }

    // 4. EspNowManager initialization
    if ((err = init_espnow()) != ESP_OK) {
        return err;
    }
    comm_.set_channel_policy(espnow::ChannelPolicy::FIXED);

    // 5. PowerControl initialization
    if ((err = power_.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PowerControl: %s", esp_err_to_name(err));
        return err;
    }
    power_.turn_on();

    // 6. Sensor initialization
    if ((err = sensor_.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Sensor: %s", esp_err_to_name(err));
        return err;
    }

    // 7. OTA Manager initialization
    if ((err = init_ota_manager()) != ESP_OK) {
        return err;
    }

    // 8. Check pending OTA verification if any
    if (ota_manager_.check_pending_verify()) {
        check_firmware();
    }

    // 9. Configure WiFi & Remote UDP Logging if requested
    if (is_logging) {
        init_logger();
    }

    return ESP_OK;
}

void WaterTankApp::run()
{
    while (true) {
        esp_err_t err;

        // Arm triggers for OTA
        btn_trigger_.arm(*this);
        espnow_trigger_.arm(*this);

        // Power on sensor and wait for warmup
        power_.turn_on();
        // rtos_.task_delay(pdMS_TO_TICKS(SENSOR_WARMUP_MS));

        // 2. Perform sensor reading
        ultrasonic::Reading reading = sensor_.read_level();

        // Turn off sensor power as soon as we have the reading
        power_.turn_off();

        // 3. Process logic (Brain)
        logic_.process_reading(reading, stats_);
        logic_.update_operation_mode(stats_);

        // 4. Read battery status
        if (battery_monitor_.init() == ESP_OK) {
            battery_monitor::BatteryReading bat_reading;
            if (battery_monitor_.read(bat_reading) == ESP_OK) {
                logic_.process_battery(bat_reading.voltage_mv, stats_);
                battery_monitor_.deinit();
            }
        }

        ESP_LOGI(
            TAG,
            "Distance: %.1f - UsResult %d - Permile: %d | Battery: %d | FillState: %d",
            reading.cm,
            static_cast<int>(reading.result),
            stats_.level_permille,
            stats_.last_battery_mv,
            static_cast<int>(stats_.fill_state));

        // 5. Transmit data to Hub
        err = send_report();
        if (err != ESP_OK || comm_.get_node_state() == espnow::NodeState::RECOVERY_SCAN) {
            if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
                ESP_LOGI(TAG, "Retrying to send report after channel recovery...");
                send_report();
            }
        }

        // 6. Listen for incoming messages (e.g. START_OTA, SLEEP_OVERRIDE) before sleeping
        uint64_t override_sleep_us = listen_for_messages(LISTEN_WINDOW_MS);

        if (ota_triggered_) {
            process_pending_ota();
        }

        uint64_t sleep_time_us = (override_sleep_us > 0) ? override_sleep_us : logic_.calculate_sleep_time_us(stats_);

        // 7. Determine GPIO wakeup status to save in NVS
        stats_.gpio_wakeup_enabled = float_switch_.should_enable_wakeup();

        // 8. Save updated state (Single NVS write)
        if (core_storage_.save_core(core_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save stats to core storage");
        }
        if (tank_storage_.save_app_data(stats_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save stats to tank storage");
        }

        // 9. Enter deep sleep
        enter_deep_sleep(sleep_time_us);
        // rtos_.task_delay(pdMS_TO_TICKS(5000));
    }
}

// =====================================================================
// PRIVATE METHODS
// =====================================================================

esp_err_t WaterTankApp::send_report()
{
    farm::WaterLevelReport report = {};

    report.level_permille = stats_.level_permille;
    report.distance_cm = stats_.last_distance_cm;
    report.battery_mv = stats_.last_battery_mv;
    report.battery_percent = stats_.last_battery_percent;
    report.battery_state = stats_.last_battery_state;
    uint8_t status_val = static_cast<uint8_t>(map_status(stats_.last_result));
    status_val = (status_val == 0xFF) ? 0x0F : (status_val & 0x0F);
    report.status = static_cast<farm::SensorStatus>(status_val | (static_cast<uint8_t>(stats_.fill_state) << 4));

    report.float_switch_is_full = float_switch_.is_tank_full();
    report.backup_mode_active = stats_.backup_mode_active;

    esp_err_t err = comm_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::WATER_LEVEL_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send report: %s", esp_err_to_name(err));
    }
    return err;
}

farm::SensorStatus WaterTankApp::map_status(ultrasonic::UsResult result)
{
    switch (result) {
    case ultrasonic::UsResult::OK:
        return farm::SensorStatus::OK;
    case ultrasonic::UsResult::WEAK_SIGNAL:
        return farm::SensorStatus::WARNING_LOW_SIGNAL;
    case ultrasonic::UsResult::TIMEOUT:
        return farm::SensorStatus::ERROR_TIMEOUT;
    case ultrasonic::UsResult::OUT_OF_RANGE:
        return farm::SensorStatus::ERROR_OUT_OF_RANGE;
    case ultrasonic::UsResult::HIGH_VARIANCE:
    case ultrasonic::UsResult::INSUFFICIENT_SAMPLES:
        return farm::SensorStatus::ERROR_UNSTABLE;
    case ultrasonic::UsResult::ECHO_STUCK:
    case ultrasonic::UsResult::HW_FAULT:
        return farm::SensorStatus::ERROR_HARDWARE;
    default:
        return farm::SensorStatus::UNKNOWN;
    }
}

void WaterTankApp::enter_deep_sleep(uint64_t sleep_time_us)
{
    ESP_LOGI(TAG, "Entering deep sleep for %llu s", sleep_time_us / 1000000);

    // Disarm triggers before going to sleep
    btn_trigger_.disarm();
    espnow_trigger_.disarm();

    disconnect_stop_wifi();

    sleep_.disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    bool wakeup_configured = false;

    if (stats_.gpio_wakeup_enabled) {
        int gpio_num;
        bool wake_high;
        if (float_switch_.get_wakeup_config(gpio_num, wake_high) == ESP_OK) {
            uint64_t pin_mask = 1ULL << gpio_num;
            idf_hals::GpioWakeupMode mode =
                wake_high ? idf_hals::GpioWakeupMode::HIGH_LEVEL : idf_hals::GpioWakeupMode::LOW_LEVEL;
            if (sleep_.deep_sleep_enable_gpio_wakeup(pin_mask, mode) == ESP_OK) {
                wakeup_configured = true;
                ESP_LOGI(TAG, "GPIO wakeup enabled on pin %d (wake_on_high=%d)", gpio_num, wake_high);
            }
            else {
                ESP_LOGE(TAG, "Failed to enable GPIO wakeup!");
            }
        }
    }

    if (sleep_time_us > 0) {
        if (sleep_.enable_timer_wakeup(sleep_time_us) == ESP_OK) {
            wakeup_configured = true;
        }
        else {
            ESP_LOGE(TAG, "Failed to enable timer wakeup!");
        }
    }

    if (!wakeup_configured) {
        ESP_LOGE(TAG, "No wakeup sources could be configured! Restarting system to prevent permanent sleep.");
        system_hal_.restart();
        return;
    }

    sleep_.deep_sleep_start();
}

bool WaterTankApp::wait_for_comm_ready(uint32_t timeout_ms)
{
    espnow::NodeState state = comm_.get_node_state();

    if (state == espnow::NodeState::RECOVERY_SCAN) {
        constexpr uint32_t POLL_DELAY_MS = 100;
        int64_t deadline_ms = (sys_timer_.get_time_us() / 1000) + timeout_ms;

        while ((sys_timer_.get_time_us() / 1000) < deadline_ms) {
            rtos_.task_delay(pdMS_TO_TICKS(POLL_DELAY_MS));
            state = comm_.get_node_state();

            if (state == espnow::NodeState::OPERATIONAL) {
                ESP_LOGI(TAG, "ESP-NOW recovered channel during wait window");
                return true;
            }
        }
    }

    if (state != espnow::NodeState::OPERATIONAL) {
        ESP_LOGE(TAG, "ESP-NOW NodeState not ready after wait: %d", static_cast<int>(state));
        return false;
    }

    return true;
}

uint64_t WaterTankApp::listen_for_messages(uint32_t timeout_ms)
{
    uint64_t override_sleep_us = 0;

    if (!rx_queue_) {
        rtos_.task_delay(pdMS_TO_TICKS(timeout_ms));
        return 0;
    }

    int64_t deadline_ms = (sys_timer_.get_time_us() / 1000) + timeout_ms;
    espnow::AppMessage msg;

    while ((sys_timer_.get_time_us() / 1000) < deadline_ms) {
        int64_t remaining = deadline_ms - (sys_timer_.get_time_us() / 1000);
        if (remaining <= 0)
            break;

        if (rtos_.queue_receive(rx_queue_, &msg, pdMS_TO_TICKS(remaining)) == pdPASS) {
            if (msg.msg_type == espnow::MessageType::COMMAND) {
                if (process_command(msg, override_sleep_us)) {
                    break;
                }
            }
        }
    }

    return override_sleep_us;
}

bool WaterTankApp::process_command(const espnow::AppMessage& msg, uint64_t& out_override_sleep_us)
{
    const auto payload_type = msg.payload_type;

    // Generic transport commands (0x01–0x3F)
    if (payload_type <= 0x3F) {
        auto cmd = static_cast<espnow::CommandType>(payload_type);
        if (cmd == espnow::CommandType::START_OTA) {
            ESP_LOGW(TAG, "Received START_OTA command from Hub - triggering OTA");
            static_cast<EspNowOtaTrigger&>(espnow_trigger_).notify();
        }
        else if (cmd == espnow::CommandType::REBOOT) {
            ESP_LOGW(TAG, "Received REBOOT command from Hub");
            disconnect_stop_wifi();
            system_hal_.restart();
        }
    }
    // Farm application commands (0x40–0xFF)
    else {
        auto cmd = static_cast<farm::CommandType>(payload_type);
        if (cmd == farm::CommandType::SLEEP_OVERRIDE) {
            if (msg.payload_len >= sizeof(farm::SleepOverrideCommand)) {
                farm::SleepOverrideCommand sleep_cmd{};
                memcpy(&sleep_cmd, msg.payload, sizeof(sleep_cmd));
                out_override_sleep_us = static_cast<uint64_t>(sleep_cmd.sleep_time_s) * 1000000ULL;
                ESP_LOGI(TAG, "Received SLEEP_OVERRIDE: %lu s", static_cast<unsigned long>(sleep_cmd.sleep_time_s));
                return true;
            }
        }
    }
    return false;
}

void WaterTankApp::process_pending_ota()
{
    ESP_LOGI(TAG, "Processing pending OTA...");
    bool wifi_ready = true;
    bool connected_by_us = false;

    if (wifi_.get_state() != wifi_manager::State::CONNECTED_GOT_IP) {
        ESP_LOGI(TAG, "WiFi not connected. Connecting for OTA...");
        if (wifi_.connect(OTA_WIFI_CONNECT_TIMEOUT_MS) == ESP_OK) {
            connected_by_us = true;
        }
        else {
            ESP_LOGE(TAG, "Failed to connect to WiFi for OTA");
            wifi_ready = false;
        }
    }

    if (wifi_ready) {
        ota_manager_.start_ota();
        uint32_t elapsed_ms = 0;
        OtaStatus status = ota_manager_.get_status();

        while (status != OtaStatus::READY_TO_RESTART && status != OtaStatus::FAILED &&
               elapsed_ms < OTA_WATCHDOG_TIMEOUT_MS) {
            rtos_.task_delay(pdMS_TO_TICKS(500));
            elapsed_ms += 500;
            status = ota_manager_.get_status();
        }

        if (status == OtaStatus::READY_TO_RESTART) {
            ESP_LOGI(TAG, "OTA completed. Disconnecting WiFi if connected and restarting safely.");
            disconnect_stop_wifi();
            system_hal_.restart();
        }
        else {
            ESP_LOGE(TAG, "OTA failed or timed out. Cancelling OTA.");
            ota_manager_.cancel_ota();
            if (connected_by_us) {
                ESP_LOGI(TAG, "Disconnecting WiFi connected by OTA...");
                wifi_.disconnect(2000);
            }
        }
    }
    ota_triggered_ = false;
}

esp_err_t WaterTankApp::disconnect_stop_wifi()
{
    esp_err_t ret = ESP_OK;
    if (wifi_.get_state() != wifi_manager::State::UNINITIALIZED &&
        wifi_.get_state() != wifi_manager::State::INITIALIZED) {
        ESP_LOGI(TAG, "Ensuring WiFi is disconnected and stopped...");
        ret = wifi_.disconnect(2000);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = wifi_.stop(2000);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ret;
}

// =============================================================================
// Init Helpers
// =============================================================================

esp_err_t WaterTankApp::init_wifi()
{
    esp_err_t err;
    if ((err = wifi_.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = wifi_.add_credentials(WIFI_SSID, WIFI_PASS)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi credentials: %s", esp_err_to_name(err));
    }
    if ((err = wifi_.start()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFiManager: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t WaterTankApp::init_espnow()
{
    espnow::EspNowConfig config;
    config.node_id = static_cast<espnow::NodeId>(farm::NodeId::WATER_TANK);
    config.node_type = static_cast<espnow::NodeType>(farm::NodeType::SENSOR);
    config.app_rx_queue = rx_queue_;
    config.wifi_channel = 1;
    config.heartbeat_interval_ms = 0;

    esp_err_t err;
    if ((err = comm_.init(config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize EspNowManager: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t WaterTankApp::init_ota_manager()
{
    OtaConfig ota_config{
        .device_type = "water_tank",
        .manifest_url = SERVER_URL,
        .task_stack_size = 8192,
        .task_priority = 5,
        .transport = {.manifest_timeout_ms = 30000, .firmware_timeout_ms = 30000},
        .security = {.allow_http_during_development = true},
        .allow_same_version = false,
        .restart_on_success = false,
    };

    if (!ota_manager_.init(ota_config)) {
        ESP_LOGE(TAG, "Failed to initialize OTA Manager");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void WaterTankApp::check_firmware()
{
    ESP_LOGI(TAG, "New firmware pending verification. Confirming as valid.");
    if (ota_manager_.confirm_app_valid()) {
        ESP_LOGI(TAG, "Firmware confirmed successfully.");
    }
    else {
        ESP_LOGE(TAG, "Failed to confirm firmware. Triggering rollback.");
        disconnect_stop_wifi();
        ota_manager_.rollback_and_reboot();
    }
}

void WaterTankApp::init_logger()
{
    ESP_LOGI(TAG, "Connecting to WiFi synchronously for remote logging...");

    esp_err_t err;
    if ((err = wifi_.connect(15000)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi for logging: %s", esp_err_to_name(err));
    }
    else {
        wifi_.disconnect(2000);
        wifi_.connect(15000);
        comm_.set_channel_policy(espnow::ChannelPolicy::FIXED);
        while (wifi_.get_state() != wifi_manager::State::CONNECTED_GOT_IP) {
            ESP_LOGE(TAG, "Waiting for WiFi connection.");
            rtos_.task_delay(pdMS_TO_TICKS(200));
        }

        udp_logger::init("192.168.1.23", 4444);
    }
}

esp_err_t WaterTankApp::init_core_storage()
{
    esp_err_t ret;
    ret = core_storage_.load_core(core_);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded core storage");
        process_boot_reasons();
        return ESP_OK;
    }

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = create_default_core_storage();
        if (ret == ESP_OK) {
            process_boot_reasons();
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Failed to load core storage: %s", esp_err_to_name(ret));
    return ret;
}

void WaterTankApp::process_boot_reasons()
{
    core_.boot_count++;

    esp_reset_reason_t reason = system_hal_.reset_reason();
    switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:

        core_.crash_count++;
        pending_core_commit_ = true;
        core_.last_wake = WakeSource::CRASH;
        ESP_LOGW(TAG, "Reset from crash.");
        break;

    case ESP_RST_POWERON:
        core_.last_wake = WakeSource::POWER_ON;
        break;

    case ESP_RST_SW:
        core_.last_wake = WakeSource::RESTART;
        break;

    case ESP_RST_DEEPSLEEP:
        process_wakeup_cause();
        break;

    default:
        core_.last_wake = WakeSource::UNKNOWN;
        break;
    }
}

void WaterTankApp::process_wakeup_cause()
{
    esp_sleep_wakeup_cause_t cause = sleep_.get_wakeup_cause();
    switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
        core_.last_wake = WakeSource::TIMER;
        break;

    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
    case ESP_SLEEP_WAKEUP_GPIO:
        core_.last_wake = WakeSource::GPIO;
        break;

    default:
        core_.last_wake = WakeSource::UNKNOWN;
    }
}

esp_err_t WaterTankApp::create_default_core_storage()
{
    core_.reset();
    core_.node_id = farm::NodeId::WATER_TANK;
    core_.node_type = farm::NodeType::SENSOR;
    core_.power_profile = PowerProfile::DEEP_SLEEP;

    esp_err_t ret = core_storage_.save_core(core_, /*force_nvs_commit=*/true);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS Core not found. Created new default core storage");
    }
    else {
        ESP_LOGE(TAG, "Failed to create new core storage: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t WaterTankApp::init_tank_storage()
{
    esp_err_t ret;
    ret = tank_storage_.load_app_data(stats_);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded tank stats from storage");
        return ESP_OK;
    }

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = create_default_tank_storage();
        if (ret == ESP_OK) {
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Failed to initialize tank storage: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t WaterTankApp::create_default_tank_storage()
{
    stats_.reset();
    esp_err_t ret = tank_storage_.save_app_data(stats_, /*force_nvs_commit=*/true);

    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "NVS Tank not found. Created new default tank storage");
    }
    else {
        ESP_LOGE(TAG, "Failed to create new tank storage: %s", esp_err_to_name(ret));
    }

    return ret;
}