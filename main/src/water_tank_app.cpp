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

static const char* TAG = "WaterTankApp";

WaterTankApp::WaterTankApp(
    INvsCore& core_storage,
    IWaterTankNvs& tank_storage,
    ILevelSensor& sensor,
    floatswitch::IFloatSwitch& float_switch,
    espnow::IEspNowManager& comm,
    QueueHandle_t rx_queue,
    ITankCommandHandler& command_handler,
    power_control::IPowerControl& power,
    idf_hals::ISleepHAL& sleep,
    battery_monitor::IBatteryMonitor& battery_monitor,
    idf_hals::ITimerHAL& sys_timer,
    idf_hals::IHalFreertos& rtos,
    WaterTankLogic& logic,
    wifi_manager::IWiFiManager& wifi,
    IOtaController& ota_controller,
    IOtaTrigger& btn_trigger,
    idf_hals::ISystemHAL& system_hal,
    time_manager::ITimeManager& time_manager,
    ILedController& led_controller)
    : core_storage_(core_storage)
    , tank_storage_(tank_storage)
    , sensor_(sensor)
    , float_switch_(float_switch)
    , espnow_(comm)
    , rx_queue_(rx_queue)
    , command_handler_(command_handler)
    , power_(power)
    , sleep_(sleep)
    , battery_monitor_(battery_monitor)
    , sys_timer_(sys_timer)
    , rtos_(rtos)
    , logic_(logic)
    , wifi_(wifi)
    , ota_controller_(ota_controller)
    , btn_trigger_(btn_trigger)
    , system_hal_(system_hal)
    , time_manager_(time_manager)
    , led_controller_(led_controller)
{
}

void WaterTankApp::on_ota_triggered(OtaTriggerSource source)
{
    ESP_LOGI(TAG, "OTA triggered from source: %d", static_cast<int>(source));
    ota_triggered_ = true;
    led_controller_.set_pattern(BlinkPattern::OTA_UPDATING);
}

esp_err_t WaterTankApp::init(bool is_logging)
{
    esp_err_t err;

    // 0. Initialize and start Status LED Controller
    if ((err = led_controller_.init()) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize LedController: %s", esp_err_to_name(err));
    }
    led_controller_.start();

    // 1. OTA Controller first to handle OTA updates
    if ((err = init_ota_controller()) != ESP_OK) {
        led_controller_.set_pattern(BlinkPattern::ERROR_BURST);
        return err;
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
        if (wifi_.connect(CONNECT_WIFI_TIMEOUT_MS, /* max attempts = */ 3) == ESP_OK) {
            espnow_.set_channel_policy(espnow::ChannelPolicy::FIXED); // Can be used even before comm init()
            udp_logger::init(UDP_LOG_SERVER_IP, UDP_LOG_PORT);
            rtos_.task_delay(pdMS_TO_TICKS(5000));
        }
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

    update_running_version();

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

    // 9. Rollback if session is not healthy
    if (!session_healthy_) {
        led_controller_.set_pattern(BlinkPattern::ERROR_BURST);
        if (ota_controller_.check_pending_verify()) {
            ESP_LOGE(TAG, "Session is not healthy during OTA verification.");
            check_firmware_healthy();
        }
        return ESP_FAIL;
    }

    led_controller_.set_pattern(BlinkPattern::BOOT_SUCCESS);
    return ESP_OK;
}

bool WaterTankApp::run(bool enter_sleep)
{
    // 1. Power on sensor rail & check node state / arm triggers
    esp_err_t pwr_err = power_.turn_on();
    int64_t power_on_time_ms = sys_timer_.get_time_us() / 1000;

    process_node_state();

    btn_trigger_.arm(*this);

    // 2. Read auxiliary sensors (FloatSwitch & Battery) while sensor warms up
    floatswitch_tank_full_ = float_switch_.is_tank_full();

    if (battery_monitor_.init() == ESP_OK) {
        battery_monitor::BatteryReading bat_reading;
        if (battery_monitor_.read(bat_reading) == ESP_OK) {
            stats_.last_battery_mv = bat_reading.voltage_mv;
            stats_.last_battery_percent = bat_reading.percent;
            stats_.last_battery_state = static_cast<farm::BatteryState>(bat_reading.state);
            battery_monitor_.deinit();
        }
    }
    ESP_LOGI(
        TAG,
        "Aux sensors: Battery %u mV (%u%%), Float switch full: %d",
        stats_.last_battery_mv,
        stats_.last_battery_percent,
        floatswitch_tank_full_);

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
        ESP_LOGI(TAG, "Ultrasonic reading: %.1f cm (Result: %d)", reading.cm, static_cast<int>(reading.result));
    }
    else {
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
        "Tank state: Level %u ‰ | FillState: %d | BackupMode: %d",
        stats_.level_permille,
        static_cast<int>(stats_.fill_state),
        stats_.backup_mode_active);

    // 5. Transmit report to Hub (enqueues packet to TX task)
    farm::WaterLevelReport report = create_report();
    esp_err_t send_err = send_report(report);

    // 6. Check send status & wait for channel recovery & retry
    if (send_err == ESP_OK) {
        ESP_LOGI(TAG, "Report sent to Hub");
    }
    else {
        ESP_LOGW(TAG, "Failed to send report on first attempt: %s", esp_err_to_name(send_err));
        led_controller_.set_pattern(BlinkPattern::ERROR_BURST);
        if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
            send_err = send_report(report);
            if (send_err == ESP_OK) {
                ESP_LOGI(TAG, "Report sent to Hub on second attempt.");
            }
        }
    }
    if (send_err != ESP_OK) {
        led_controller_.set_pattern(BlinkPattern::ERROR_BURST);
        ESP_LOGE(TAG, "Failed to send report: %s", esp_err_to_name(send_err));
    }

    // 7. Listen for incoming messages & commands
    TankCommandProcessResult cmd_res = command_handler_.process(LISTEN_WINDOW_MS);

    if (cmd_res.time_synced) {
        core_.has_valid_time = time_manager_.is_synchronized();
        core_.last_sync_unix_time_ms = time_manager_.get_timestamp_ms();
        pending_core_commit_ = true;
    }

    if (cmd_res.reboot_requested) {
        ESP_LOGW(TAG, "Reboot requested via command; persisting state and restarting...");
        save_persistent_state();
        rtos_.task_delay(pdMS_TO_TICKS(100));
        espnow_.deinit();
        wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
        wifi_.stop(DISCONNECT_WIFI_TIMEOUT_MS);
        system_hal_.restart();
        return false;
    }

    // 8. Handle OTA triggers & firmware verification
    if (cmd_res.ota_requested || ota_triggered_) {
        process_pending_ota();
    }

    if (ota_controller_.check_pending_verify()) {
        check_firmware_healthy();
    }

    // 9. Calculate sleep time & determine GPIO wakeup status
    bool is_night = false;
    if (time_manager_.is_synchronized()) {
        time_t sec = time_manager_.get_timestamp_sec();
        struct tm tm_info;
        if (localtime_r(&sec, &tm_info) != nullptr) {
            is_night = (tm_info.tm_hour >= NIGHT_START_HOUR || tm_info.tm_hour < NIGHT_END_HOUR);
        }
    }

    uint64_t sleep_time_us =
        (cmd_res.override_sleep_us > 0) ? cmd_res.override_sleep_us : logic_.calculate_sleep_time_us(stats_, is_night);
    stats_.gpio_wakeup_enabled = float_switch_.should_enable_wakeup();

    // 10. Save updated state (Core & Tank Storage)
    save_persistent_state();

    // 11. Enter deep sleep (or delay if enter_sleep is false for testing)
    if (enter_sleep) {
        enter_deep_sleep(sleep_time_us);
        return false; // Never reached
    }
    return true;
}

// =====================================================================
// PRIVATE METHODS & HELPERS
// =====================================================================

static farm::SensorStatus map_status(ultrasonic::UsResult result)
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

farm::WaterLevelReport WaterTankApp::create_report() const
{
    farm::WaterLevelReport report = {};

    report.power_profile = core_.power_profile;
    report.level_permille = stats_.level_permille;
    report.distance_cm = stats_.last_distance_cm;
    report.battery_mv = stats_.last_battery_mv;
    report.battery_percent = stats_.last_battery_percent;
    report.battery_state = stats_.last_battery_state;
    report.status = map_status(stats_.last_result);
    report.float_switch_is_full = floatswitch_tank_full_;
    report.backup_mode_active = stats_.backup_mode_active;
    report.unix_time = stats_.sample_timestamp_ms;

    return report;
}

esp_err_t WaterTankApp::send_report(const farm::WaterLevelReport& report)
{
    led_controller_.set_pattern(BlinkPattern::TX_PULSE);
    return espnow_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<espnow::PayloadType>(farm::PayloadType::WATER_LEVEL_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );
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

void WaterTankApp::enter_deep_sleep(uint64_t sleep_time_us)
{
    ESP_LOGI(TAG, "Entering deep sleep for %llu s", sleep_time_us / 1000000);

    led_controller_.stop();

    // Disarm triggers before going to sleep
    btn_trigger_.disarm();

    espnow_.deinit();
    wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
    wifi_.stop(DISCONNECT_WIFI_TIMEOUT_MS);

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
    espnow::NodeState state = espnow_.get_node_state();

    if (state == espnow::NodeState::RECOVERY_SCAN || state == espnow::NodeState::IDLE) {
        constexpr uint32_t POLL_DELAY_MS = 100;
        int64_t deadline_ms = (sys_timer_.get_time_us() / 1000) + timeout_ms;

        while ((sys_timer_.get_time_us() / 1000) < deadline_ms) {
            rtos_.task_delay(pdMS_TO_TICKS(POLL_DELAY_MS));
            state = espnow_.get_node_state();

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

    espnow::NodeState state = espnow_.get_node_state();

    while ((sys_timer_.get_time_us() / 1000) < deadline_ms) {
        rtos_.task_delay(pdMS_TO_TICKS(POLL_DELAY_MS));
        state = espnow_.get_node_state();

        if (state == espnow::NodeState::OPERATIONAL) {
            return;
        }
    }
    if (state != espnow::NodeState::OPERATIONAL) {
        ESP_LOGE(TAG, "ESP-NOW NodeState NOT PAIRED after wait");
    }
}

void WaterTankApp::process_pending_ota()
{
    ota_triggered_ = false;

    ESP_LOGI(TAG, "Processing pending OTA...");

    // 1. Deinit radio users before OTA
    espnow_.deinit();
    btn_trigger_.disarm();

    bool previous_connected = (wifi_.get_state() == wifi_manager::State::CONNECTED_GOT_IP);
    bool wifi_ok = previous_connected;

    if (!previous_connected) {
        wifi_ok = (wifi_.connect(CONNECT_WIFI_TIMEOUT_MS, /* max attempts = */ 3) == ESP_OK);
    }

    OtaActionResult ota_res{};

    static constexpr uint32_t OTA_WATCHDOG_TIMEOUT_MS = 120000;

    if (wifi_ok) {
        ota_res = ota_controller_.execute_download(OTA_WATCHDOG_TIMEOUT_MS);
        if (ota_res.success) {
            ESP_LOGI(TAG, "OTA completed successfully. Restarting...");
            wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
            wifi_.stop(DISCONNECT_WIFI_TIMEOUT_MS);
            system_hal_.restart();
            return;
        }
        led_controller_.set_pattern(BlinkPattern::ERROR_BURST);
    }
    else {
        ESP_LOGE(TAG, "Failed to connect to WiFi for OTA");
        ota_res.success = false;
        ota_res.exec_result = farm::OtaExecResult::DOWNLOAD_FAILED;
        ota_res.error_code = farm::OtaErrorCode::WIFI_CONNECT_FAILED;
        led_controller_.set_pattern(BlinkPattern::ERROR_BURST);
    }

    if (!previous_connected) {
        wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
    }
    if (init_espnow() == ESP_OK) {
        espnow_.set_channel_policy(previous_connected ? espnow::ChannelPolicy::FIXED : espnow::ChannelPolicy::SCAN);
        send_ota_report(ota_res.exec_result, ota_res.error_code);
    }
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
    config.heartbeat_interval_ms = 60000 * 5; // 5 minutes
    config.enable_heartbeat = false;
    config.logical_ack_retries = 2;
    config.ack_timeout_ms = 350;

    return espnow_.init(config);
}

esp_err_t WaterTankApp::init_ota_controller()
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

    if (!ota_controller_.init(ota_config)) {
        ESP_LOGE(TAG, "Failed to initialize OTA Controller");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void WaterTankApp::check_firmware_healthy()
{
    OtaActionResult verify_res = ota_controller_.confirm_firmware(session_healthy_);

    if (!verify_res.success) {
        ESP_LOGE(
            TAG,
            "Failed to confirm firmware. Triggering rollback (reason: %d).",
            static_cast<int>(verify_res.error_code));
        led_controller_.set_pattern(BlinkPattern::ERROR_BURST);

        if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
            send_ota_report(verify_res.exec_result, verify_res.error_code);
        }
        wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
        wifi_.stop(DISCONNECT_WIFI_TIMEOUT_MS);
        ota_controller_.rollback_and_reboot();
        return;
    }

    pending_core_commit_ = true;
    ESP_LOGI(TAG, "Firmware confirmed successfully. Version: %d.%d.%d", core_.fw_major, core_.fw_minor, core_.fw_patch);
    led_controller_.set_pattern(BlinkPattern::BOOT_SUCCESS);

    if (wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
        send_ota_report(verify_res.exec_result, verify_res.error_code);
    }
}

esp_err_t WaterTankApp::init_core_storage()
{
    CoreData default_core = {};
    default_core.reset();
    default_core.node_id = farm::NodeId::WATER_TANK;
    default_core.node_type = farm::NodeType::SENSOR;
    default_core.power_profile = farm::PowerProfile::DEEP_SLEEP;

    esp_err_t ret = core_storage_.init(core_, default_core);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize core storage: %s", esp_err_to_name(ret));
        return ret;
    }

    core_storage_.process_boot_reasons(
        core_, system_hal_.reset_reason(), sleep_.get_wakeup_cause(), pending_core_commit_);

    return ESP_OK;
}

esp_err_t WaterTankApp::init_tank_storage()
{
    WaterTankStats default_stats = {};
    default_stats.reset();

    esp_err_t ret = tank_storage_.init_app_data(stats_, default_stats);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize tank storage: %s", esp_err_to_name(ret));
        return ret;
    }

    stats_.cycles_since_nvs_commit++;
    return ESP_OK;
}

void WaterTankApp::process_node_state()
{
    espnow::NodeState state = espnow_.get_node_state();
    if (state == espnow::NodeState::OPERATIONAL) {
        ESP_LOGI(TAG, "ESP-NOW NodeState OPERATIONAL");
        if (led_controller_.get_current_pattern() == BlinkPattern::PAIRING_MODE) {
            led_controller_.set_pattern(BlinkPattern::OFF);
        }
        return;
    }

    if (state == espnow::NodeState::PAIRING || state == espnow::NodeState::PAIRING_SCAN) {
        ESP_LOGI(TAG, "ESP-NOW NodeState PAIRING");
        led_controller_.set_pattern(BlinkPattern::PAIRING_MODE);
        esp_err_t ret = espnow_.add_peer(espnow::ReservedIds::HUB, HUB_MAC, espnow::ReservedTypes::HUB, 0);
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "HUB added as peer");
            led_controller_.set_pattern(BlinkPattern::OFF);
            return;
        }
        wait_for_pairing(PAIRING_TIMEOUT_MS);
        return;
    }
}

esp_err_t WaterTankApp::send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code)
{
    if (!wait_for_comm_ready(RECOVERY_SCAN_WAIT_MS)) {
        return ESP_ERR_INVALID_STATE;
    }

    farm::OtaStatusReport report = {};
    report.power_profile = core_.power_profile;
    report.result = result;
    report.error_code = error_code;
    report.fw_major = core_.fw_major;
    report.fw_minor = core_.fw_minor;
    report.fw_patch = core_.fw_patch;

    ESP_LOGI(TAG, "Sending OTA status report: result=%u, error_code=%u", result, error_code);
    return espnow_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );
}

void WaterTankApp::update_running_version()
{
    auto current_version = ota_controller_.get_running_version();

    if (current_version.has_value()) {
        if (core_.fw_major != current_version->major || core_.fw_minor != current_version->minor ||
            core_.fw_patch != current_version->patch) {
            core_.fw_major = current_version->major;
            core_.fw_minor = current_version->minor;
            core_.fw_patch = current_version->patch;
            pending_core_commit_ = true; // Garante que a nova versão será gravada no NVS
        }
    }
    ESP_LOGI(TAG, "Running version: %u.%u.%u", core_.fw_major, core_.fw_minor, core_.fw_patch);
}
