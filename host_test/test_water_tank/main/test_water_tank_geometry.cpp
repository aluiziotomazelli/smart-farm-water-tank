#include <gtest/gtest.h>
#include "tank_geometry.hpp"

TEST(TankGeometryTest, FullLevelReturns1000)
{
    TankGeometry geometry(20); // 20cm offset
    // 20cm distance = 0cm depth = full
    EXPECT_EQ(geometry.calculate_permille(20.0f), 1000);
}

TEST(TankGeometryTest, BottomLevelReturns0)
{
    TankGeometry geometry(20);
    // 170cm distance = 150cm depth = empty
    EXPECT_EQ(geometry.calculate_permille(170.0f), 0);
}

TEST(TankGeometryTest, MidPointInterpolation)
{
    TankGeometry geometry(20);
    // 30cm depth (distance 50cm) is exactly the first breakpoint
    EXPECT_EQ(geometry.calculate_permille(50.0f), 751);
}
