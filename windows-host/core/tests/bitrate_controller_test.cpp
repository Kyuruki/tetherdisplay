// Portable unit tests for the bitrate controller (M5).
#include "td/core/bitrate_controller.hpp"

#include <gtest/gtest.h>

using td::core::BitrateController;

TEST(BitrateController, CongestionDecreasesAndClampsToFloor) {
  BitrateController c(50000, /*floor=*/10000, /*cap=*/80000);
  EXPECT_EQ(c.current(), 50000u);
  EXPECT_EQ(c.OnCongestion(), 35000u);  // x0.7
  EXPECT_EQ(c.OnCongestion(), 24500u);
  for (int i = 0; i < 20; ++i) c.OnCongestion();
  EXPECT_EQ(c.current(), 10000u);  // never below floor
}

TEST(BitrateController, HealthyIncreasesAndClampsToCap) {
  BitrateController c(50000, 10000, 80000);
  EXPECT_EQ(c.OnHealthy(), 54000u);  // +5% of cap (4000)
  for (int i = 0; i < 50; ++i) c.OnHealthy();
  EXPECT_EQ(c.current(), 80000u);  // never above cap
}

TEST(BitrateController, StartClampedIntoRange) {
  EXPECT_EQ(BitrateController(999999, 10000, 80000).current(), 80000u);
  EXPECT_EQ(BitrateController(1, 10000, 80000).current(), 10000u);
}
