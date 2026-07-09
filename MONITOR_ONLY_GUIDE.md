# Guide: Monitor-Only Mode (Without Physical Float Switch)

If you are deploying the system for monitoring only and cannot connect a physical float switch, follow these steps to ensure the battery is protected and the logic remains consistent.

## 1. Prevent Battery Drain in Backup Mode

In the event of an ultrasonic sensor failure, the system enters **Backup Mode**. Without a physical float switch, the system might default to a state of "Tank Not Full," causing it to wake up every 15 seconds to retry the sensor, which will quickly drain the battery.

To prevent this, configure the `FloatSwitch` in `water_tank_app.cpp` so that an open (disconnected) pin is interpreted as **"Tank Full"** (triggering the longer 5-minute sleep).

### Recommended Configuration

Update the `ProductionStack` in `app_water_tank/main/src/water_tank_app.cpp`:

```cpp
// Change ActiveLevel to HIGH so that the internal pull-up (HIGH) 
// is interpreted as "Tank Full".
floatswitch::FloatSwitch fs{
    {FLOAT_SWITCH_GPIO,
     true,                            // enable_internal_pullup
     50000,                           // debounce_us
     floatswitch::ActiveLevel::HIGH,  // interpreting pull-up as FULL
     floatswitch::WakeupCondition::NEVER}, // disable pin wakeup
    gpio_hal_fs,
    timer_hal};
```

## 2. Summary of Configuration Values

| Parameter | Recommended Value | Reason |
| :--- | :--- | :--- |
| `enable_internal_pullup` | `true` | Keeps the pin at a stable HIGH state when disconnected. |
| `active_level` | `HIGH` | Matches the pull-up state to signal "Full" and save battery. |
| `wakeup_condition` | `NEVER` | Prevents the open pin from causing accidental wakeups from noise. |

## 3. Telemetry Impact

With this configuration:
- The `float_switch_is_full` field in the ESP-NOW report will always be `true`.
- If the ultrasonic sensor fails, the system will sleep for `TIMER_STABLE_US` (5 minutes) instead of the 15-second retry timer, preserving your battery until you can fix the sensor.
