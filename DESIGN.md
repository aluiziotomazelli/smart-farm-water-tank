# Smart Farm - Water Tank Node Architecture

## 1. Overview

The **Water Tank Node** is a battery-powered, deep-sleep edge node within the Smart Farm ecosystem. Its primary function is to accurately measure water levels, monitor battery health, execute edge application logic, and transmit telemetry reports to the central Gateway/Hub via ESP-NOW.

---

## 2. System Architecture & Lifecycle

*(Architecture overview and lifecycle components to be documented in future updates)*

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

*(Storage architecture, NVS, and RTC caching details to be populated)*

---

## 5. Power & Deep Sleep Management

*(Power profiles, GPIO wakeup, and backup mode logic to be populated)*

---

## 6. Over-The-Air (OTA) Updates

*(OTA triggers, manifest parsing, and rollback mechanism to be populated)*
