// app_water_tank/main/include/tank_geometry.hpp
#pragma once
#include <cstdint>

/**
 * @class TankGeometry
 * @brief Handles mathematical conversions for water tank measurements.
 *
 * Converts raw sensor distance readings into a volume fraction (permille)
 * using a piecewise-linear lookup table. The tank is modelled as a stack of
 * cylindrical segments, each with a different cross-section, so the
 * volume-to-height relationship is non-linear.
 *
 * ## Coordinate system
 * All internal calculations use *depth* — the distance from the full-water
 * line downwards (i.e. sensor_distance - offset_cm). Depth 0 = tank full,
 * depth = last LUT entry = tank empty.
 *
 * ## Field calibration
 * If the actual usable maximum water level (e.g. the overflow drain) differs
 * from the assumed top of the tank, adjust only two values in VOLUME_LUT:
 *   - Entry [0].depth_cm stays 0 (always the "full" anchor).
 *   - Entry [N-1].depth_cm to the real depth of the tank bottom from the
 *     drain level.
 * No changes to the interpolation algorithm are needed.
 */
class TankGeometry
{
public:
    /**
     * @brief Construct a new Tank Geometry object.
     *
     * @param sensor_offset_cm Distance (cm) from the sensor to the water
     *                         surface when the tank is at its maximum usable
     *                         level (e.g. overflow drain). This is the only
     *                         runtime calibration parameter.
     */
    explicit TankGeometry(uint8_t sensor_offset_cm)
        : offset_cm_(sensor_offset_cm)
    {
    }

    /**
     * @brief Convert a sensor distance reading to a volume permille (0–1000).
     *
     * @param distance_cm Raw distance reading from the ultrasonic sensor (cm).
     * @return uint16_t  Volume fraction: 0 = empty, 1000 = full (at drain
     *                   level). Clamped to [0, 1000].
     */
    uint16_t calculate_permille(float distance_cm) const;

private:
    /**
     * @brief One breakpoint in the piecewise-linear volume lookup table.
     *
     * `depth_cm` is the distance below the full-water line at which the
     * breakpoint occurs. `permille` is the corresponding volume fraction.
     * Entries MUST be sorted in ascending `depth_cm` order.
     * The first entry MUST have depth_cm = 0 (full) and permille = 1000.
     * The last entry MUST have permille = 0 (empty).
     */
    struct LutEntry
    {
        uint8_t  depth_cm; ///< Depth below the full-water line (cm).
        uint16_t permille; ///< Volume fraction at this depth (0–1000).
    };

    /**
     * @brief Piecewise-linear volume lookup table.
     *
     * Physical tank: 5 stacked cylinders of increasing diameter from base to top.
     * All depths are measured **downward from the full-water line** (drain level).
     *
     * ## Field calibration
     * If the actual drain level differs from the currently assumed top (150 cm),
     * the **entire LUT must be recalculated** — both `depth_cm` and `permille`:
     *
     *   1. Measure `offset_cm_`: sensor distance when water is at the drain.
     *   2. For each physical breakpoint at height H from the bottom:
     *        depth_cm = drain_height - H
     *   3. Recalculate each `permille` based on the new segment volumes
     *      (the top segment is shorter, so all fractions shift).
     *
     * The algorithm does not need to change — only the 6 pairs of values below.
     *
     * Current assumption: drain at 150 cm (= physical top of tank).
     * Depths measured downward from that level:
     *   [0]   0 cm  → 1000 ppm  (full / drain)
     *   [1]  30 cm  →  751 ppm  (step 5/4 boundary at 120 cm from bottom)
     *   [2]  60 cm  →  528 ppm  (step 4/3 boundary at  90 cm from bottom)
     *   [3]  90 cm  →  329 ppm  (step 3/2 boundary at  60 cm from bottom)
     *   [4] 120 cm  →  153 ppm  (step 2/1 boundary at  30 cm from bottom)
     *   [5] 150 cm  →    0 ppm  (bottom / empty)
     */
    static constexpr LutEntry VOLUME_LUT[] = {
        {  0, 1000}, // full line (drain / top)
        { 30,  751}, // step 5/4 boundary
        { 60,  528}, // step 4/3 boundary
        { 90,  329}, // step 3/2 boundary
        {120,  153}, // step 2/1 boundary
        {150,    0}, // bottom (empty)
    };

    static constexpr uint8_t LUT_SIZE = sizeof(VOLUME_LUT) / sizeof(VOLUME_LUT[0]);

    const uint8_t offset_cm_; ///< Sensor distance (cm) when tank is at full level.

    /**
     * @brief Interpolate volume permille from a depth value using the LUT.
     *
     * Performs a linear scan to find the surrounding LUT entries and applies
     * continuous linear interpolation. Clamps to [0, 1000] outside the table.
     *
     * @param depth_cm  Depth below the full-water line (cm). Must be >= 0.
     * @return float    Volume fraction in permille (0.0–1000.0).
     */
    float depth_to_permille(float depth_cm) const;
};
