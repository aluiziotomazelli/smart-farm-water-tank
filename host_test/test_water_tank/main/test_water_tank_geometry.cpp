#include <gtest/gtest.h>
#include "tank_geometry.hpp"

TEST(TankGeometryTest, FullLevelReturns1000)
{
    TankGeometry geometry(28); // ~28.5cm offset
    // 28cm distance = 0cm depth = full
    EXPECT_EQ(geometry.calculate_permille(28.0f), 1000);
}

TEST(TankGeometryTest, BottomLevelReturns0)
{
    TankGeometry geometry(28);
    // 169cm distance = 141cm depth = empty
    EXPECT_EQ(geometry.calculate_permille(169.0f), 0);
}

TEST(TankGeometryTest, MidPointInterpolation)
{
    TankGeometry geometry(28);
    // 22cm depth (distance 50cm) is exactly the first breakpoint
    EXPECT_EQ(geometry.calculate_permille(50.0f), 835);
}