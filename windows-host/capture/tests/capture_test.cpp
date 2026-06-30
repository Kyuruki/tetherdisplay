// Portable unit tests for the capture module's pure policy (adapter selection + cross-adapter
// decision). The WGC/D3D11 capture path is exercised on hardware by sandboxes/capture_sandbox.cpp.
#include "td/capture/adapter.hpp"
#include "td/capture/capture_source.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace td::capture;

TEST(CrossAdapter, SameAdapterNeedsNoCopy) {
  EXPECT_FALSE(NeedsCrossAdapterCopy(AdapterId{1, 0}, AdapterId{1, 0}));
}

TEST(CrossAdapter, DifferentAdaptersNeedCopy) {
  EXPECT_TRUE(NeedsCrossAdapterCopy(AdapterId{1, 0}, AdapterId{2, 0}));
  EXPECT_TRUE(NeedsCrossAdapterCopy(AdapterId{1, 0}, AdapterId{1, 7}));  // high part differs
}

TEST(SelectEncoderAdapter, EmptyReturnsNullopt) {
  std::vector<AdapterInfo> none;
  EXPECT_FALSE(SelectEncoderAdapter(none).has_value());
}

TEST(SelectEncoderAdapter, PrefersNvidiaEvenWithLessVram) {
  // Intel iGPU advertises more "dedicated" VRAM than the dGPU here; NVIDIA must still win.
  std::vector<AdapterInfo> a{
      {AdapterId{1, 0}, kVendorIntel, 8ull << 30},
      {AdapterId{2, 0}, kVendorNvidia, 4ull << 30},
  };
  auto pick = SelectEncoderAdapter(a);
  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->vendor_id, kVendorNvidia);
  EXPECT_EQ(pick->id, (AdapterId{2, 0}));
}

TEST(SelectEncoderAdapter, AmongNvidiaPicksMostVram) {
  std::vector<AdapterInfo> a{
      {AdapterId{2, 0}, kVendorNvidia, 8ull << 30},
      {AdapterId{3, 0}, kVendorNvidia, 16ull << 30},
      {AdapterId{1, 0}, kVendorIntel, 2ull << 30},
  };
  auto pick = SelectEncoderAdapter(a);
  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->id, (AdapterId{3, 0}));
}

TEST(SelectEncoderAdapter, NoNvidiaFallsBackToMostVram) {
  std::vector<AdapterInfo> a{
      {AdapterId{1, 0}, kVendorIntel, 2ull << 30},
      {AdapterId{4, 0}, kVendorAmd, 12ull << 30},
  };
  auto pick = SelectEncoderAdapter(a);
  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->id, (AdapterId{4, 0}));  // AMD with more VRAM
}

TEST(CaptureStatus, ToStringCoversAll) {
  for (auto s : {CaptureStatus::Ok, CaptureStatus::MonitorNotFound, CaptureStatus::DeviceCreateFailed,
                 CaptureStatus::WgcUnavailable, CaptureStatus::CrossAdapterFailed,
                 CaptureStatus::StartFailed}) {
    EXPECT_STRNE(to_string(s), "Unknown");
  }
}
