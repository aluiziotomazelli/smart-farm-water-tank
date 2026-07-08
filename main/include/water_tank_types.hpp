#pragma once
#include "farm_protocol_types.hpp"
#include <cstdint>
// #include "common_types.hpp"

// Constants for water tank logic
// NOTE: TANK_HEIGHT_CM is no longer passed to TankGeometry. The usable tank
// depth is now encoded directly in the last entry of TankGeometry::VOLUME_LUT.
// Kept here for documentation and future NVS/calibration use.
static constexpr uint8_t TANK_HEIGHT_CM = 150;  ///< Nominal total height of the tank (cm). See TankGeometry::VOLUME_LUT.
static constexpr uint8_t SENSOR_OFFSET_CM = 20; ///< Offset of the sensor from the top of water max level (cm)

static constexpr float SENSOR_MIN_DISTANCE_CM = 15.0f;  ///< Minimum distance for sensor valid measure
static constexpr float SENSOR_MAX_DISTANCE_CM = 200.0f; ///< Maximum distance for sensor valid measure

static constexpr uint16_t LEVEL_DELTA_MIN = 5; ///< Minimum level change to detect (5 ppm)

static constexpr uint64_t TIMER_FILLING_US = 60ULL * 1000000ULL;       ///< Time to sleep when tank is filling (60 s)
static constexpr uint64_t TIMER_STABLE_US = 5ULL * 60ULL * 1000000ULL; ///< Time to sleep when tank is stable (5 min)
static constexpr uint64_t TIMER_DRAIN_US = 2ULL * 60ULL * 1000000ULL;  ///< Time to sleep when tank is draining (2 min)
static constexpr uint64_t TIMER_UNKNOWN_US = 60ULL * 1000000ULL; ///< Time to sleep when tank state is unknown (60 s)

static constexpr float WEAK_SLEEP_FACTOR = 0.5f;     ///< Factor to reduce sleep time when sensor reading is weak
static constexpr float INVALID_SLEEP_FACTOR = 0.25f; ///< Factor to reduce sleep time when sensor reading is invalid

static constexpr uint8_t CONSECUTIVE_FAILURES_THRESHOLD = 5; ///< Number of consecutive failures to enter backup mode
static constexpr uint64_t BACKUP_MODE_SLEEP_US = 15ULL * 1000000ULL; ///< Time to sleep in backup mode (15 s)

enum class FillState : uint8_t
{
    UNKNOWN = 0,
    STABLE,
    FILLING,
    DRAINING
};
