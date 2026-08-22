# Smart Farm - Water Tank Node Architecture

## 1. Overview

The **Water Tank Node** is a battery-powered, deep-sleep edge node within the Smart Farm ecosystem. Its primary function is to accurately measure water levels, monitor battery health, execute edge application logic, and transmit telemetry reports to the central Gateway/Hub via ESP-NOW.

---

## 2. System Architecture & Lifecycle

The node implements a deterministic **run-to-completion** operational model designed to minimize active MCU runtime:

```
[Reset / Wakeup] ──► [Init (LED, Storage, WiFi, ESP-NOW, Sensors)]
                           │
                           ▼
                    [Sensor Rail ON] ──► [Aux Read (Battery, FloatSwitch)]
                           │
                           ▼
                    [Ultrasonic Multi-Sample] ──► [Sensor Rail OFF]
                           │
                           ▼
                    [Edge Logic & Fill State Confirmation]
                           │
                           ▼
                    [ESP-NOW WaterLevelReport Transmission (LED TX Pulse)]
                           │
                           ▼
                    [Listen Window (200 ms for Hub Commands)]
                           │
                           ▼
                    [Save State & Stop LED Controller]
                           │
                           ▼
                    [Enter Deep Sleep (Timer + FloatSwitch Wakeup)]
```

### 2.1 Dependency Injection & HAL Decoupling
All hardware-specific APIs (GPIO, ADC, Sleep, Timers, FreeRTOS, NVS, Wi-Fi, System) are abstracted through pure virtual interfaces (`idf_hals` and `smart-farm-common`). The core application orchestrator (`WaterTankApp`) and domain logic (`WaterTankLogic`, `TankGeometry`) have zero direct dependencies on ESP-IDF hardware drivers, enabling comprehensive unit testing on host Linux.

---

## 3. Time Synchronization & Clock Architecture

### 3.1 Design Principles

- **Offline / ESP-NOW Synchronization**: The node does not maintain a continuous Wi-Fi/internet connection for SNTP polling. Time synchronization is received wirelessly from the Hub via ESP-NOW protocol messages.
- **Hardware RTC Persistence**: The ESP32 system clock (`gettimeofday` / POSIX time) is maintained continuously across deep sleep cycles by the hardware RTC memory timer.
- **No Time Regression**: The system does NOT restore cached timestamps from NVS to the system clock on boot. This prevents clock skew and backward time jumps upon waking up.

---

### 3.2 Initialization & Setup

- **Dependency Injection**: The `TimeManager` (`components/time_manager`) is instantiated in `main.cpp` with its HAL dependencies (`HalSntp`, `HalSystemTime`) and injected by reference into `WaterTankApp`.
- **Encapsulated Setup**: During `WaterTankApp::init()`, the private helper `init_time_manager()` configures:
  - Timezone: POSIX string `<-04>4` (UTC-4, no DST).
  - SNTP: `use_dhcp_sntp = false` (local SNTP client disabled).

---

### 3.3 ESP-NOW Time Synchronization Workflow

```
[Hub] ──(ESP-NOW: SYNC_TIME)──> [WaterTankApp::listen_for_messages]
                                             │
                                             ▼
                             [WaterTankApp::process_command]
                                             │
                                             ▼
                        [WaterTankApp::sync_time_from_espnow]
                                             │
                       ┌─────────────────────┴─────────────────────┐
                       ▼                                           ▼
       [time_manager_.sync_from_time_packet]          [CoreStorage Update]
    (Sets System POSIX / ESP32 RTC Clock)        (has_valid_time = true,
                                                  last_sync_unix_time_ms)
```

1. **Message Reception**: During the execution window (`listen_for_messages()`), incoming messages are queued and evaluated by `process_command()`.
2. **Command Processing**: When a payload of `farm::CommandType::SYNC_TIME` (`0x43`) is received, the payload is cast to `farm::TimeSyncCommand`.
3. **Clock Update**: `sync_time_from_espnow()` converts the command to a `time_manager::TimeSyncPacket` (`sync_source = TimeSyncSource::ESP_NOW`) and passes it to `time_manager_.sync_from_time_packet(pkt)`.
4. **Audit State Commit**: On success, `CoreStorage` updates `has_valid_time = true` and `last_sync_unix_time_ms = timestamp_ms` for audit tracking, setting `pending_core_commit_ = true`.

---

### 3.4 Telemetry Sampling & Transmission

1. **Sensor Sample Timestamping**: In `WaterTankApp::run()`, immediately after reading the ultrasonic sensor, the node checks `time_manager_.is_synchronized()`:
   - If synchronized: `stats_.sample_timestamp_ms` = `time_manager_.get_timestamp_ms()`.
   - If unsynchronized: `stats_.sample_timestamp_ms` = `0`.
2. **Telemetry Report Transmission**: In `send_report()`, `farm::WaterLevelReport::unix_time` is populated with `stats_.sample_timestamp_ms` and sent to the Hub over ESP-NOW.

---

### 3.5 Data Structures & Field Definitions

| Data Structure | Field Name | Type | Description |
|---|---|---|---|
| `WaterTankStats` | `sample_timestamp_ms` | `uint64_t` | UTC epoch timestamp (ms) recorded at the exact moment of sensor reading in the current cycle. |
| `CoreStorage` | `has_valid_time` | `bool` | Persistence flag indicating whether the node has received at least one valid time synchronization. |
| `CoreStorage` | `last_sync_unix_time_ms` | `uint64_t` | Audit timestamp (ms) recording the epoch time when the last ESP-NOW sync occurred. |
| `farm::WaterLevelReport` | `unix_time` | `uint64_t` | Sample timestamp (ms) included in the telemetry payload sent to the Hub (`0` if unsynchronized). |

---

## 4. State Management & Storage

### 4.1 Storage Architecture
The node utilizes the `AppStorage<T, Magic, Version>` template pattern (`smart-farm-common`) with `StorageEnvelope`:
- **RTC Memory / RAM Caching**: Runtime state resides in memory and persists across deep sleep wakeups.
- **NVS Flash Throttling**: To maximize Flash endurance, commits to non-volatile storage are throttled to every `NVS_COMMIT_INTERVAL = 10` cycles (~50 minutes in normal operation) unless forced by critical events (e.g. firmware confirmation, time sync, reboot command).
- **Integrity Validation**: CRC32 checksums, magic words (`0x57544E56` for tank stats, `0x434F5245` for core), and schema versioning prevent corrupted reads.

---

## 5. Power & Deep Sleep Management

### 5.1 Power Gating
- The ultrasonic sensor is powered via a high-side P-MOSFET gate on `GPIO 10`. Power is asserted at the start of `run()` and deactivated immediately after valid sampling, reducing sensor standby draw from ~15 mA to 0 µA during sleep.

### 5.2 Dynamic Sleep Interval Calculation
`WaterTankLogic::calculate_sleep_time_us()` determines deep sleep duration according to operational dynamics:
- **Filling / Draining Trend**: Accelerated sampling (~60s) to closely track rapid volume changes.
- **Stable Reservoir**: Extended sampling (300s / 5 min) for battery conservation.
- **Critical Battery / Error State**: Fallback backoff interval to prevent brownout loops.

### 5.3 Float Switch GPIO Wakeup
When the reservoir is empty or falling, `FloatSwitch::should_enable_wakeup()` enables the ESP32-C3 deep sleep GPIO wakeup on `GPIO 2`. A rising water level triggering the float switch immediately wakes the MCU without waiting for timer expiry.

---

## 6. Over-The-Air (OTA) Updates

### 6.1 Trigger Mechanisms
- **Hardware Trigger**: Long press of the BOOT button (`GPIO 9`) captured by `ButtonOtaTrigger`.
- **ESP-NOW Network Trigger**: Remote command (`farm::CommandType::START_OTA`) captured by `EspNowOtaTrigger`.

### 6.2 Execution & Rollback Protection
1. **Radio Handover**: ESP-NOW is stopped and Wi-Fi connects to the local AP using stored credentials.
2. **Download & Flash**: `OtaController` fetches the manifest and streams the firmware binary to the next OTA partition.
3. **Post-Boot Health Check**: Upon rebooting into the new image, `WaterTankApp::check_firmware_healthy()` verifies all peripheral inits. If healthy, the partition is confirmed (`esp_ota_mark_app_valid_cancel_rollback()`). If any init fails, the partition is rejected and the bootloader automatically rolls back to the previous stable firmware slot.
