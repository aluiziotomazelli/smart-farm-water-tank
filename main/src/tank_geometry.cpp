// app_water_tank/main/src/tank_geometry.cpp

#include "tank_geometry.hpp"

static const char* TAG = "TankGeometry";

uint16_t TankGeometry::calculate_permille(float distance_cm) const
{
    // Convert sensor distance to depth below the full-water line.
    // Depth 0  → tank full (at drain/offset level)
    // Depth max → tank empty (at bottom)
    float depth = distance_cm - static_cast<float>(offset_cm_);

    float permille_f = depth_to_permille(depth);

    // Single rounding at the very end for protocol compatibility (uint16_t)
    return static_cast<uint16_t>(permille_f + 0.5f);
}

// =====================================================================
// Private methods
// =====================================================================

float TankGeometry::depth_to_permille(float depth_cm) const
{
    // Clamp: at or above the full-water line
    if (depth_cm <= static_cast<float>(VOLUME_LUT[0].depth_cm)) {
        return static_cast<float>(VOLUME_LUT[0].permille);
    }

    // Clamp: at or below the tank bottom
    if (depth_cm >= static_cast<float>(VOLUME_LUT[LUT_SIZE - 1].depth_cm)) {
        return static_cast<float>(VOLUME_LUT[LUT_SIZE - 1].permille);
    }

    // Linear scan to find the surrounding LUT entries.
    // The LUT is small (6 entries), so a simple loop is sufficient.
    for (uint8_t i = 0; i < LUT_SIZE - 1; i++) {
        float h0 = static_cast<float>(VOLUME_LUT[i].depth_cm);
        float h1 = static_cast<float>(VOLUME_LUT[i + 1].depth_cm);

        if (depth_cm <= h1) {
            float p0 = static_cast<float>(VOLUME_LUT[i].permille);
            float p1 = static_cast<float>(VOLUME_LUT[i + 1].permille);

            // Continuous linear interpolation — works for any segment width
            return p0 + (p1 - p0) * (depth_cm - h0) / (h1 - h0);
        }
    }

    // Unreachable: both clamps above cover all remaining cases
    return 0.0f;
}