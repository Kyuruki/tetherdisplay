// Portable unit tests for the host config (M6).
#include "td/core/config.hpp"

#include <gtest/gtest.h>

using namespace td::core;

TEST(Config, RoundTrips) {
  Config c;
  c.device_port = 4000;
  c.use_hevc = false;
  c.target_bitrate_kbps = 40000;
  c.max_bitrate_kbps = 60000;
  c.fps = 30;
  EXPECT_EQ(ParseConfig(SerializeConfig(c)), c);
}

TEST(Config, ClampsOutOfRange) {
  auto c = ParseConfig("device_port=0\nfps=999\nmax_bitrate_kbps=500000\ntarget_bitrate_kbps=400000\n");
  EXPECT_EQ(c.device_port, 2345);                          // 0 -> default
  EXPECT_EQ(c.fps, 240u);                                  // capped
  EXPECT_EQ(c.max_bitrate_kbps, td::encode::kUsb2VideoCeilingKbps);  // USB-2 ceiling
  EXPECT_EQ(c.target_bitrate_kbps, td::encode::kUsb2VideoCeilingKbps);  // can't exceed max
}

TEST(Config, TargetClampedBelowMax) {
  auto c = ParseConfig("max_bitrate_kbps=30000\ntarget_bitrate_kbps=90000\n");
  EXPECT_EQ(c.max_bitrate_kbps, 30000u);
  EXPECT_EQ(c.target_bitrate_kbps, 30000u);  // target <= max
}

TEST(Config, OutOfRangePortKeepsDefaultAndMalformedLinesSkipped) {
  // device_port > 65535 must not truncate-wrap; malformed lines are skipped without crashing.
  auto c = ParseConfig("device_port=70000\nno_equals_line\n=missingkey\nfps=\n");
  EXPECT_EQ(c.device_port, 2345);  // 70000 rejected -> default (not the wrapped 4464)
  EXPECT_EQ(c.fps, 60u);           // empty value ignored -> default
}

TEST(Config, UnknownKeysAndCommentsIgnored) {
  auto c = ParseConfig("# a comment\nfuture_key=whatever\nfps=24  # inline comment\n");
  EXPECT_EQ(c.fps, 24u);
  EXPECT_EQ(c.device_port, 2345);  // unchanged default
}

TEST(Config, CodecParsing) {
  EXPECT_FALSE(ParseConfig("codec=h264").use_hevc);
  EXPECT_TRUE(ParseConfig("codec=hevc").use_hevc);
  EXPECT_TRUE(ParseConfig("codec=anything-else").use_hevc);  // default to HEVC
}

TEST(Config, MissingFileReturnsNullopt) {
  EXPECT_FALSE(LoadConfig("/tmp/td-nonexistent-config-xyz.conf").has_value());
}
