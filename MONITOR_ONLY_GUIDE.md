# Guide: Monitor-Only Mode (Without Physical Float Switch)

If you are deploying the system for monitoring only and cannot connect a physical float switch, follow these steps to ensure the battery is protected and the logic remains consistent.

## 1. Prevent Battery Drain in Backup Mode

In the event of an ultrasonic sensor failure, the system enters **Backup Mode**. Without a physical float switch, the system might default to a state of "Tank Not Full," causing it to wake up every 15 seconds to retry the sensor, which will quickly drain the battery.

To prevent this, configure the `FloatSwitch` in `water_tank_app.cpp` so that an open (disconnected) pin is interpreted as **"Tank Full"** (triggering the longer 5-minute sleep).

### Recommended Configuration

Update the `float_switch_config` in `smart-farm-water-tank/main/main.cpp`:

```cpp
floatswitch::Config float_switch_config = {
    .gpio = FLOAT_SWITCH_GPIO,
    .normally_open = true, // Open contact = Tank Full
    .debounce_time_us = 50000,
    .active_level = floatswitch::ActiveLevel::LOW, // internal pull-up will be enabled naturally yielding an open contact
    .wakeup_on = floatswitch::WakeupCondition::NEVER}; // Disable pin wakeup to prevent noise from waking the device
```

## 2. Summary of Configuration Values

| Parameter | Recommended Value | Reason |
| :--- | :--- | :--- |
| `normally_open` | `true` | Ensures that an open/disconnected circuit is interpreted as "Full". |
| `active_level` | `LOW` | Automatically enables the internal pull-up resistor. When the pin is disconnected, it stays HIGH, resulting in an "open" reading. |
| `wakeup_on` | `NEVER` | Prevents the disconnected pin from causing accidental wakeups from electrical noise. |

## 3. Telemetry Impact

With this configuration:
- The `float_switch_is_full` field in the ESP-NOW report will always be `true`.
- If the ultrasonic sensor fails, the system will sleep for `TIMER_STABLE_US` (5 minutes) instead of the 15-second retry timer, preserving your battery until you can fix the sensor.
