// Portable unit tests for the vdisplay module's pure logic (mode selection + the Parsec remove-index
// encoding + keep-alive bound). The Win32 driver control in parsec_vdd.cpp is exercised on hardware
// by sandboxes/vdd_sandbox.cpp, not here.
#include "td/vdisplay/vdd_protocol.hpp"
#include "td/vdisplay/virtual_display.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace td::vdisplay;

TEST(VddProtocol, EncodeRemoveIndexMatchesReferenceSwap) {
  // UINT16 = ((i & 0xFF) << 8) | ((i >> 8) & 0xFF)
  EXPECT_EQ(parsec::EncodeRemoveIndex(0), 0x0000);
  EXPECT_EQ(parsec::EncodeRemoveIndex(1), 0x0100);
  EXPECT_EQ(parsec::EncodeRemoveIndex(7), 0x0700);
  EXPECT_EQ(parsec::EncodeRemoveIndex(0x0102), 0x0201);  // full byte swap
}

TEST(VddProtocol, KeepAliveStaysUnderDriverTimeout) {
  EXPECT_LT(parsec::kKeepAliveIntervalMs, 100);
  EXPECT_GT(parsec::kKeepAliveIntervalMs, 0);
}

TEST(PickBestMode, EmptyReturnsNullopt) {
  std::vector<DisplayMode> none;
  EXPECT_FALSE(PickBestMode(DisplayMode{2360, 1640, 60}, none).has_value());
}

TEST(PickBestMode, ExactMatchWins) {
  std::vector<DisplayMode> sup{{1920, 1080, 60}, {2360, 1640, 60}, {2360, 1640, 30}};
  auto m = PickBestMode(DisplayMode{2360, 1640, 60}, sup);
  ASSERT_TRUE(m);
  EXPECT_EQ(*m, (DisplayMode{2360, 1640, 60}));
}

TEST(PickBestMode, ExactResolutionNearestRefresh) {
  std::vector<DisplayMode> sup{{2360, 1640, 30}, {2360, 1640, 50}, {1920, 1080, 60}};
  auto m = PickBestMode(DisplayMode{2360, 1640, 60}, sup);
  ASSERT_TRUE(m);
  EXPECT_EQ(*m, (DisplayMode{2360, 1640, 50}));  // 50 is closer to 60 than 30
}

TEST(PickBestMode, FallsBackToClosestAreaWhenNoResolutionMatch) {
  std::vector<DisplayMode> sup{{1920, 1080, 60}, {2560, 1440, 60}, {3840, 2160, 60}};
  auto m = PickBestMode(DisplayMode{2360, 1640, 60}, sup);
  ASSERT_TRUE(m);
  EXPECT_EQ(*m, (DisplayMode{2560, 1440, 60}));  // nearest pixel area to 2360x1640
}

TEST(PickBestMode, PrefersEqualAspectRatioOverCloserArea) {
  // desired is 1:1; a same-aspect option with larger area difference should still beat a closer-area
  // option with a different aspect ratio.
  std::vector<DisplayMode> sup{{1100, 1000, 60}, {2000, 2000, 60}};
  auto m = PickBestMode(DisplayMode{1000, 1000, 60}, sup);
  ASSERT_TRUE(m);
  EXPECT_EQ(*m, (DisplayMode{2000, 2000, 60}));
}

TEST(VddStatus, ToStringCoversAll) {
  for (auto s : {VddStatus::Ok, VddStatus::DriverNotFound, VddStatus::DriverNotRunning,
                 VddStatus::OpenFailed, VddStatus::AddFailed, VddStatus::ModeUnavailable,
                 VddStatus::RemoveFailed}) {
    EXPECT_STRNE(to_string(s), "Unknown");
  }
}
