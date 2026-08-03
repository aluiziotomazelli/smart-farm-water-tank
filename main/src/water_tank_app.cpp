#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

#include "i_hal_nvs.hpp"

#include "espnow_ota_trigger.hpp"
#include "water_tank_app.hpp"
#include "core_types.hpp"
#include "farm_protocol_types.hpp"
#include "protocol_types.hpp"

#include "udp_logger.hpp"
#include "secrets.hpp"

// App Orchestrator Constants
static constexpr uint32_t SENSOR_WARMUP_MS = 600;
static constexpr uint8_t DEFAULT_SAMPLE_COUNT = 11;

static constexpr uint32_t LISTEN_WINDOW_MS = 200;
static constexpr uint32_t NVS_COMMIT_INTERVAL = 10;

static constexpr uint32_t RECOVERY_SCAN_WAIT_MS =
    espnow::SCAN_CHANNEL_TIMEOUT_MS * espnow::SCAN_CHANNEL_ATTEMPTS * 13 + 200;
static constexpr uint32_t PAIRING_TIMEOUT_MS = 60000;

static constexpr uint16_t CONNECT_WIFI_TIMEOUT_MS = 15000;
static constexpr uint16_t DISCONNECT_WIFI_TIMEOUT_MS = 2000;
static constexpr uint32_t OTA_WATCHDOG_TIMEOUT_MS = 120000;

static constexpr uint8_t HUB_MAC_ADRESS[] = {HUB_MAC_0, HUB_MAC_1, HUB_MAC_2, HUB_MAC_3, HUB_MAC_4, HUB_MAC_5};

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
    idf_hals::ISystemHAL& system_hal,
    time_manager::ITimeManager& time_manager)
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
    , time_manager_(time_manager)
{
}

void WaterTankApp::on_ota_triggered(OtaTriggerSource source)
{
    ESP_LOGI(TAG, "OTA triggered from source: %d", static_cast<int>(source));
    ota_triggered_ = true;
}

esp_err_t WaterTankApp::init(bool is_logging)
{
    esp_err_t err;

    // 1. OTA Manager first to handle OTA updates
    if ((err = init_ota_manager()) != ESP_OK) {
        return err;
    }

    if (ota_manager_.check_pending_verify()) {
        pending_firmware_verify_ = true;
    }

    // 2. wifi for esp-now and OTA
    if ((err = init_wifi()) != ESP_OK) {
        session_healthy_ = false;
        ESP_LOGE(TAG, "Failed to initialize WiFiManager: %s", esp_err_to_name(err));
    }

    if ((err = init_time_manager()) != ESP_OK) {
        session_healthy_ = false;
        ESP_LOGE(TAG, "Failed to initialize TimeManager: %s", esp_err_to_name(err));
    }

    // 8. Connect WiFi & Remote UDP Logging if requested
    if (is_logging) {
        if (connect_wifi_with_retry() == ESP_OK) {
            comm_.set_channel_policy(espnow::ChannelPolicy::FIXED); // Can be used even before comm init()
            udp_logger::init("192.168.1.23", 4444);
            rtos_.task_delay(pdMS_TO_TICKS(5000));
        }
    }

    auto version = ota_manager_.get_running_version();
    if (version.has_value()) {
        ESP_LOGI(TAG, "Firmware version: %d.%d.%d", version->major, version->minor, version->patch);
    }

    // 3. FloatSwitch is safest than sensor
    if ((err = float_switch_.init()) != ESP_OK) {
        session_healthy_ = false;
        ESP_LOGE(TAG, "Failed to initialize FloatSwitch: %s", esp_err_to_name(err));
    }

    // 4. Storage / NVS Data load
    if ((err = init_core_storage()) != ESP_OK) {
        session_healthy_ = false;
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
    }
    if ((err = init_tank_storage()) != ESP_OK) {
        session_healthy_ = false;
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
    }

    // 5. EspNowManager initialization
    if ((err = init_espnow()) != ESP_OK) {
        session_healthy_ = false;
        ESP_LOGE(TAG, "Failed to initialize EspNowManager: %s", esp_err_to_name(err));
    }

    // 6. PowerControl initialization
    if ((err = power_.init()) != ESP_OK) {
        session_healthy_ = false;
        ESP_LOGE(TAG, "Failed to initialize PowerControl: %s", esp_err_to_name(err));
    }

    // 7. Sensor initialization
    if (err == ESP_OK) { // if power is on, init sensor
        if ((err = sensor_.init()) != ESP_OK) {
            session_healthy_ = false;
            ESP_LOGE(TAG, "Failed to initialize Sensor: %s", esp_err_to_name(err));
        }
    }

    // 9. Roolback if session is not healthy
    if (!session_healthy_) {
        if (pending_firmware_verify_) {
            ESP_LOGE(TAG, "Session is not healthy during OTA verification.");
            check_firmware();
        }
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool WaterTankApp::run(bool enter_sleep)
{
    // 1. Power on sensor rail & check node state / arm triggers
    esp_err_t pwr_err = power_.turn_on();
    int64_t power_on_time_ms = sys_timer_.get_time_us() / 1000;

    process_node_state();

    btn_trigger_.arm(*this);
    espnow_trigger_.arm(*this);

    // 2. Read auxiliary sensors (FloatSwitch & Battery) while sensor warms up
    floatswitch_tank_full_ = float_switch_.is_tank_full();

    if (battery_monitor_.init() == ESP_OK) {
        battery_monitor::BatteryReading bat_reading;
        if (battery_monitor_.read(bat_reading) == ESP_OK) {
            logic_.process_battery(bat_reading.voltage_mv, stats_);
            battery_monitor_.deinit();
        }
    }

    // 3. Perform ultrasonic reading (wait remaining warmup if needed)
    ultrasonic::Reading reading;
    if (pwr_err == ESP_OK) {
        int64_t elapsed_ms = (sys_timer_.get_time_us() / 1000) - power_on_time_ms;
        if (elapsed_ms < SENSOR_WARMUP_MS) {
            uint32_t remaining_warmup = SENSOR_WARMUP_MS - static_cast<uint32_t>(elapsed_ms);
            rtos_.task_delay(pdMS_TO_TICKS(remaining_warmup));
        }
        reading = sensor_.read_level(DEFAULT_SAMPLE_COUNT);
        retry_reading_if_needed(reading);
        power_.turn_off();
    }
    else { // if sensor was not powered on
        ESP_LOGE(TAG, "Failed to power on sensor: %s", esp_err_to_name(pwr_err));

        reading.result = ultrasonic::UsResult::HW_FAULT;
        reading.cm = 0;

        session_healthy_ = false;
    }

    // 4. Process application logic & update stats
    stats_.sample_timestamp_ms = time_manager_.is_synchronized() ? time_manager_.get_timestamp_ms() : 0;
    logic_.process_reading(reading, stats_);
    logic_.update_operation_mode(stats_);

    ESP_LOGI(
        TAG,
        "Distance: %.1f - UsResult %d - Permile: %d | Battery: %d | FillState: %d | Time: %lld",
        reading.cm,
        static_cast<int>(reading.result),
        stats_.level_permille,
        stats_.last_battery_mv,
        static_cast<int>(stats_.fill_state),
        stats_.sample_timestamp_ms);

    // 5. Transmit report to Hub (enqueues packet to TX task)
    esp_err_t send_err = send_report();

    // 6. Listen for incoming messages (gives time for background TX & ACK processing)
    uint64_t override_sleep_us = listen_for_messages(LISTEN_WINDOW_MS);

    // 7. Check comm status (if TX failed or node entered RECOVERY_SCAN, wait for channel recovery & retry)
    if (send_err != ESP_OK || comm_.get_node_state() == espnow::NodeState::RECOVERY_SCAN) {
        if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
            ESP_LOGI(TAG, "Channel recovered! Retrying report send...");
            send_report();
            rtos_.task_delay(pdMS_TO_TICKS(50));
        }
    }

    // 8. Handle OTA triggers & firmware verification
    if (ota_triggered_) {
        process_pending_ota();
    }

    if (pending_firmware_verify_) {
        check_firmware();
    }

    // 9. Calculate sleep time & determine GPIO wakeup status
    uint64_t sleep_time_us = (override_sleep_us > 0) ? override_sleep_us : logic_.calculate_sleep_time_us(stats_);
    stats_.gpio_wakeup_enabled = float_switch_.should_enable_wakeup();

    // 10. Save updated state (Core & Tank Storage)
    save_persistent_state();

    // 11. Enter deep sleep (or delay if enter_sleep is false for testing)
    if (enter_sleep) {
        enter_deep_sleep(sleep_time_us);
        return false;
    }
    return true;
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
    report.status = map_status(stats_.last_result);
    report.backup_mode_active = stats_.backup_mode_active;
    report.unix_time = stats_.sample_timestamp_ms;

    esp_err_t err = comm_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<espnow::PayloadType>(farm::PayloadType::WATER_LEVEL_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send report: %s", esp_err_to_name(err));
    }
    return err;
}

void WaterTankApp::retry_reading_if_needed(ultrasonic::Reading& reading)
{
    if (reading.result == ultrasonic::UsResult::WEAK_SIGNAL) {
        uint8_t retry_count = static_cast<uint8_t>(DEFAULT_SAMPLE_COUNT * 1.4f);
        ESP_LOGW(TAG, "WEAK_SIGNAL detected. Retrying reading with %u samples", retry_count);
        reading = sensor_.read_level(retry_count);
    }
    else if (
        reading.result == ultrasonic::UsResult::HIGH_VARIANCE ||
        reading.result == ultrasonic::UsResult::INSUFFICIENT_SAMPLES) {
        uint8_t retry_count = static_cast<uint8_t>(DEFAULT_SAMPLE_COUNT * 1.8f);
        ESP_LOGW(
            TAG,
            "Unstable result (%d). Retrying reading with %u samples",
            static_cast<int>(reading.result),
            retry_count);
        reading = sensor_.read_level(retry_count);
    }
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

void WaterTankApp::save_persistent_state()
{
    bool periodic_commit = (stats_.cycles_since_nvs_commit >= NVS_COMMIT_INTERVAL);
    if (periodic_commit) {
        ESP_LOGI(TAG, "Periodic NVS commit triggered (%d cycles reached)", stats_.cycles_since_nvs_commit);
        stats_.cycles_since_nvs_commit = 0;
    }

    bool force_core = pending_core_commit_ || periodic_commit;
    bool force_tank = pending_tank_commit_ || periodic_commit;

    if (core_storage_.save_core(core_, force_core) == ESP_OK) {
        pending_core_commit_ = false;
    }
    else {
        ESP_LOGE(TAG, "Failed to save stats to core storage");
    }

    if (tank_storage_.save_app_data(stats_, force_tank) == ESP_OK) {
        pending_tank_commit_ = false;
    }
    else {
        ESP_LOGE(TAG, "Failed to save stats to tank storage");
    }
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

void WaterTankApp::wait_for_pairing(uint32_t timeout_ms)
{
    constexpr uint32_t POLL_DELAY_MS = 100;
    int64_t deadline_ms = (sys_timer_.get_time_us() / 1000) + timeout_ms;

    espnow::NodeState state = comm_.get_node_state();

    while ((sys_timer_.get_time_us() / 1000) < deadline_ms) {
        rtos_.task_delay(pdMS_TO_TICKS(POLL_DELAY_MS));
        state = comm_.get_node_state();

        if (state == espnow::NodeState::OPERATIONAL) {
            return;
        }
    }
    if (state != espnow::NodeState::OPERATIONAL) {
        ESP_LOGE(TAG, "ESP-NOW NodeState NOT PAIRED after wait");
    }
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
        else if (cmd == farm::CommandType::SYNC_TIME) {
            if (msg.payload_len >= sizeof(farm::TimeSyncCommand)) {
                farm::TimeSyncCommand farm_cmd{};
                memcpy(&farm_cmd, msg.payload, sizeof(farm_cmd));

                sync_time_from_espnow(farm_cmd);
            }
        }
    }
    return false;
}

void WaterTankApp::process_pending_ota()
{
    ESP_LOGI(TAG, "Processing pending OTA...");
    bool connected_by_us = false;

    // 1. Deinit radio users before OTA
    comm_.deinit();
    btn_trigger_.disarm();
    espnow_trigger_.disarm();

    // 2. Connect WiFi if not already connected
    if (wifi_.get_state() != wifi_manager::State::CONNECTED_GOT_IP) {
        ESP_LOGI(TAG, "WiFi not connected. Connecting for OTA...");
        if (connect_wifi_with_retry() == ESP_OK) {
            connected_by_us = true;
        }
        else {
            ESP_LOGE(TAG, "Failed to connect to WiFi for OTA");
            report_ota_failure_and_restore_comm(farm::OtaErrorCode::WIFI_CONNECT_FAILED, false);
            ota_triggered_ = false;
            return;
        }
    }

    // 3. Run OTA worker task
    ota_manager_.start_ota();
    uint32_t elapsed_ms = 0;
    OtaStatus status = ota_manager_.get_status();

    while (status != OtaStatus::READY_TO_RESTART && status != OtaStatus::FAILED &&
           elapsed_ms < OTA_WATCHDOG_TIMEOUT_MS) {
        rtos_.task_delay(pdMS_TO_TICKS(500));
        elapsed_ms += 500;
        status = ota_manager_.get_status();
    }

    // 4. Handle Outcome
    if (status == OtaStatus::READY_TO_RESTART) {
        ESP_LOGI(TAG, "OTA completed successfully. Restarting...");
        disconnect_stop_wifi();
        system_hal_.restart();
    }
    else {
        farm::OtaErrorCode err_code = farm::OtaErrorCode::UNKNOWN_ERROR;
        if (status == OtaStatus::FAILED) {
            OtaFailReason reason = ota_manager_.get_last_error();
            err_code = map_ota_fail_reason(reason);
            ESP_LOGE(TAG, "OTA failed (R:%d | C:%d)", static_cast<int>(reason), static_cast<int>(err_code));
        }
        else if (elapsed_ms >= OTA_WATCHDOG_TIMEOUT_MS) {
            err_code = farm::OtaErrorCode::WATCHDOG_TIMEOUT;
            ESP_LOGE(TAG, "OTA watchdog timeout (%u ms)", static_cast<unsigned int>(elapsed_ms));
        }

        ota_manager_.cancel_ota();
        report_ota_failure_and_restore_comm(err_code, connected_by_us);
    }

    ota_triggered_ = false;
}

esp_err_t WaterTankApp::disconnect_stop_wifi()
{
    esp_err_t ret = ESP_OK;
    if (wifi_.get_state() != wifi_manager::State::UNINITIALIZED &&
        wifi_.get_state() != wifi_manager::State::INITIALIZED) {
        ESP_LOGI(TAG, "Ensuring WiFi is disconnected and stopped...");
        ret = wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = wifi_.stop(DISCONNECT_WIFI_TIMEOUT_MS);
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

esp_err_t WaterTankApp::init_time_manager()
{
    time_manager::TimeManagerConfig config;
    config.use_dhcp_sntp = false;
    config.timezone = "<-04>4";

    esp_err_t err = time_manager_.init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TimeManager: %s", esp_err_to_name(err));
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
    if (!pending_firmware_verify_) {
        return;
    }

    if (!session_healthy_ || !ota_manager_.confirm_app_valid()) {
        farm::OtaErrorCode err =
            !session_healthy_ ? farm::OtaErrorCode::HEALTH_CHECK_FAILED : farm::OtaErrorCode::PARTITION_CONFIRM_FAILED;

        ESP_LOGE(TAG, "Failed to confirm firmware. Triggering rollback (reason: %d).", static_cast<int>(err));

        if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
            send_ota_report(farm::OtaExecResult::ROLLBACK_TRIGGERED, err);
        }
        disconnect_stop_wifi();
        ota_manager_.rollback_and_reboot();
        return;
    }

    // If we get here, the firmware is valid and confirme
    pending_firmware_verify_ = false;

    auto version = ota_manager_.get_running_version();
    if (version.has_value()) {
        core_.fw_major = version->major;
        core_.fw_minor = version->minor;
        core_.fw_patch = version->patch;
    }

    pending_core_commit_ = true;
    ESP_LOGI(TAG, "Firmware confirmed successfully. Versio: %d.%d.%d", core_.fw_major, core_.fw_minor, core_.fw_patch);

    if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
        send_ota_report(farm::OtaExecResult::CONFIRMED_SUCCESS);
    }
}

esp_err_t WaterTankApp::connect_wifi_with_retry(uint8_t max_attempts)
{
    if (wifi_.get_state() == wifi_manager::State::CONNECTED_GOT_IP) {
        return ESP_OK;
    }

    static constexpr uint16_t DELAY_BETWEEN_ATTEMPTS_MS = 1500;
    esp_err_t err = ESP_FAIL;
    for (uint8_t attempt = 1; attempt <= max_attempts; ++attempt) {
        ESP_LOGI(TAG, "Connecting to WiFi (attempt %u/%u)...", attempt, max_attempts);
        err = wifi_.connect(CONNECT_WIFI_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected successfully");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "WiFi connection attempt %u failed: %s", attempt, esp_err_to_name(err));
        if (attempt < max_attempts) {
            wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
            uint32_t delay_ms = DELAY_BETWEEN_ATTEMPTS_MS * attempt;
            rtos_.task_delay(pdMS_TO_TICKS(delay_ms));
        }
    }

    ESP_LOGE(TAG, "Failed to connect to WiFi after %u attempts: %s", max_attempts, esp_err_to_name(err));
    return err;
}

esp_err_t WaterTankApp::init_core_storage()
{
    esp_err_t ret = core_storage_.load_core(core_);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Core storage load failed (%s), recreating default storage", esp_err_to_name(ret));

        CoreStorage default_core = {};
        default_core.reset();
        default_core.node_id = farm::NodeId::WATER_TANK;
        default_core.node_type = farm::NodeType::SENSOR;
        default_core.power_profile = PowerProfile::DEEP_SLEEP;

        ret = core_storage_.create_default_storage(core_, default_core);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    else {
        ESP_LOGI(TAG, "Loaded core data from storage");
    }

    core_.boot_count++;
    core_storage_.process_boot_reasons(
        core_, system_hal_.reset_reason(), sleep_.get_wakeup_cause(), pending_core_commit_);

    return ESP_OK;
}

esp_err_t WaterTankApp::init_tank_storage()
{
    esp_err_t ret = tank_storage_.load_app_data(stats_);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded tank stats from storage");
        stats_.cycles_since_nvs_commit++;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Tank storage load failed (%s), recreating default storage", esp_err_to_name(ret));
    stats_.reset();
    ret = tank_storage_.save_app_data(stats_, /*force_nvs_commit=*/true);
    if (ret == ESP_OK) {
        stats_.cycles_since_nvs_commit++;
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to initialize tank storage: %s", esp_err_to_name(ret));
    return ret;
}

void WaterTankApp::process_node_state()
{
    espnow::NodeState state = comm_.get_node_state();
    if (state == espnow::NodeState::OPERATIONAL) {
        ESP_LOGI(TAG, "ESP-NOW NodeState OPERATIONAL");
        return;
    }

    if (state == espnow::NodeState::PAIRING || state == espnow::NodeState::PAIRING_SCAN) {
        ESP_LOGI(TAG, "ESP-NOW NodeState PAIRING");
        esp_err_t ret = comm_.add_peer(espnow::ReservedIds::HUB, HUB_MAC_ADRESS, espnow::ReservedTypes::HUB, 0);
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "HUB added as peer");
            return;
        }
        wait_for_pairing(PAIRING_TIMEOUT_MS);
        return;
    }
}

void WaterTankApp::sync_time_from_espnow(const farm::TimeSyncCommand& sync_cmd)
{
    time_manager::TimeSyncPacket pkt{};
    pkt.timestamp_ms = sync_cmd.timestamp_ms;
    pkt.tz_offset_min = sync_cmd.tz_offset_min;
    pkt.sync_source = time_manager::TimeSyncSource::ESP_NOW;
    pkt.flags = sync_cmd.flags;

    if (time_manager_.sync_from_time_packet(pkt) == ESP_OK) {
        core_.has_valid_time = true;
        core_.last_sync_unix_time_ms = pkt.timestamp_ms;
        pending_core_commit_ = true;
        ESP_LOGI(TAG, "Time synch from ESP-NOW: %llu ms", static_cast<unsigned long long>(pkt.timestamp_ms));
    }
}

esp_err_t WaterTankApp::send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code)
{
    farm::OtaStatusReport report = {};
    report.result = result;
    report.error_code = error_code;

    auto version = ota_manager_.get_running_version();
    if (version.has_value()) {
        report.fw_major = version->major;
        report.fw_minor = version->minor;
        report.fw_patch = version->patch;
    }

    ESP_LOGI(TAG, "Sending OTA status report: result=%u, error_code=%u", result, error_code);
    return comm_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );
}

farm::OtaErrorCode WaterTankApp::map_ota_fail_reason(OtaFailReason reason) const
{
    switch (reason) {
    case OtaFailReason::MANIFEST_URL_INVALID:
    case OtaFailReason::MANIFEST_INVALID:
        return farm::OtaErrorCode::MANIFEST_PARSE_ERROR;

    case OtaFailReason::MANIFEST_HTTP_FAIL:
    case OtaFailReason::FIRMWARE_URL_INVALID:
    case OtaFailReason::DOWNLOAD_HTTP_FAIL:
        return farm::OtaErrorCode::HTTP_DOWNLOAD_FAILED;

    case OtaFailReason::DEVICE_TYPE_MISMATCH:
        return farm::OtaErrorCode::DEVICE_TYPE_MISMATCH;

    case OtaFailReason::CURRENT_VERSION_PARSE_FAIL:
    case OtaFailReason::VERSION_NOT_NEWER:
    case OtaFailReason::DOWNLOAD_IMAGE_VERSION_FAIL:
        return farm::OtaErrorCode::VERSION_NOT_NEWER;

    case OtaFailReason::DOWNLOAD_SESSION_FAIL:
    case OtaFailReason::DOWNLOAD_IMAGE_DESC_FAIL:
        return farm::OtaErrorCode::DOWNLOAD_SESSION_FAIL;

    case OtaFailReason::DOWNLOAD_FINISH_FAIL:
    case OtaFailReason::HASH_PARTITION_FAIL:
        return farm::OtaErrorCode::FLASH_WRITE_ERROR;

    case OtaFailReason::HASH_MISMATCH:
        return farm::OtaErrorCode::IMAGE_HASH_MISMATCH;

    case OtaFailReason::NONE:
    default:
        return farm::OtaErrorCode::UNKNOWN_ERROR;
    }
}

void WaterTankApp::report_ota_failure_and_restore_comm(farm::OtaErrorCode err_code, bool connected_by_us)
{
    if (connected_by_us) {
        ESP_LOGI(TAG, "Disconnecting WiFi connected by OTA...");
        wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
    }

    if (init_espnow() == ESP_OK) {
        comm_.set_channel_policy(connected_by_us ? espnow::ChannelPolicy::SCAN : espnow::ChannelPolicy::FIXED);
        if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
            send_ota_report(farm::OtaExecResult::DOWNLOAD_FAILED, err_code);
        }
    }
}