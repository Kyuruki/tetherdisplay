// Portable unit tests for the encoder's rate-control policy (M1.3). The NVENC path is exercised on
// hardware by sandboxes/encode_sandbox.cpp.
#include "td/encode/encoder.hpp"

#include <gtest/gtest.h>

using namespace td::encode;

TEST(PlanRateControl, TargetBelowCapIsUsedAsIs) {
  EncoderConfig c{Codec::Hevc, 2360, 1640, 60, 50000, 80000, true};
  auto p = PlanRateControl(c);
  EXPECT_EQ(p.average_bitrate_kbps, 50000u);
  EXPECT_EQ(p.max_bitrate_kbps, 80000u);
}

TEST(PlanRateControl, TargetAboveCeilingIsClamped) {
  // Ask for 500 Mbps; the USB-2 ceiling (default 80 Mbps) must win.
  EncoderConfig c{Codec::Hevc, 2360, 1640, 60, 500000, 1000000, true};
  auto p = PlanRateControl(c);
  EXPECT_EQ(p.average_bitrate_kbps, kUsb2VideoCeilingKbps);
  EXPECT_EQ(p.max_bitrate_kbps, kUsb2VideoCeilingKbps);
}

TEST(PlanRateControl, ConfigMaxLowerThanCeilingWins) {
  EncoderConfig c{Codec::Hevc, 2360, 1640, 60, 90000, 60000, true};  // max 60 < ceiling 80
  auto p = PlanRateControl(c);
  EXPECT_EQ(p.max_bitrate_kbps, 60000u);
  EXPECT_EQ(p.average_bitrate_kbps, 60000u);  // target 90 clamped to max 60
}

TEST(PlanRateControl, LowLatencyVbvIsOneFrame) {
  EncoderConfig c{Codec::Hevc, 2360, 1640, 60, 60000, 80000, true};
  auto p = PlanRateControl(c);
  // 60000 kbps = 60,000,000 bits/s; /60 fps = 1,000,000 bits per frame.
  EXPECT_EQ(p.vbv_buffer_size_bits, 1000000u);
  EXPECT_EQ(p.vbv_initial_delay_bits, p.vbv_buffer_size_bits);
}

TEST(PlanRateControl, RelaxedVbvIsOneSecond) {
  EncoderConfig c{Codec::Hevc, 2360, 1640, 60, 60000, 80000, /*low_latency=*/false};
  auto p = PlanRateControl(c);
  EXPECT_EQ(p.vbv_buffer_size_bits, 60000000u);  // one second
}

TEST(PlanRateControl, NoBFramesAndInfiniteGop) {
  auto p = PlanRateControl(EncoderConfig{});
  EXPECT_EQ(p.frame_interval_p, 1u);  // B-frames disabled
  EXPECT_TRUE(p.infinite_gop);
}

TEST(PlanRateControl, ZeroFpsDoesNotDivideByZero) {
  EncoderConfig c{Codec::Hevc, 1280, 720, 0, 40000, 80000, true};
  auto p = PlanRateControl(c);  // must not crash
  EXPECT_GT(p.vbv_buffer_size_bits, 0u);
}

TEST(EncodeEnums, ToStringCoversAll) {
  EXPECT_STREQ(to_string(Codec::Hevc), "HEVC");
  EXPECT_STREQ(to_string(Codec::H264), "H264");
  for (auto s : {EncodeStatus::Ok, EncodeStatus::SdkNotFound, EncodeStatus::UnsupportedDevice,
                 EncodeStatus::SessionInitFailed, EncodeStatus::RegisterFailed,
                 EncodeStatus::EncodeFailed, EncodeStatus::NotInitialized}) {
    EXPECT_STRNE(to_string(s), "Unknown");
  }
}
