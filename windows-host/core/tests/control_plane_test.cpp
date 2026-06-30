// Portable unit tests for the host control plane (M4).
#include "td/core/control_plane.hpp"

#include <gtest/gtest.h>

using namespace td;

TEST(ControlPlane, KeyframeRequestForcesIdr) {
  auto a = core::HandleControlMessage(proto::KeyframeRequest{/*reason=*/1, /*last_good_seq=*/9});
  EXPECT_TRUE(a.force_idr);
  EXPECT_FALSE(a.send_pong);
  EXPECT_FALSE(a.disconnect);
}

TEST(ControlPlane, PingRequestsPongWithToken) {
  auto a = core::HandleControlMessage(proto::Ping{0xABCDEF12});
  EXPECT_TRUE(a.send_pong);
  EXPECT_EQ(a.pong_token, 0xABCDEF12u);
  EXPECT_FALSE(a.force_idr);
}

TEST(ControlPlane, HelloCapturesClientCaps) {
  proto::Hello hello{1, proto::kMaskHEVC, 2360, 1640, 80000, 60, 0};
  auto a = core::HandleControlMessage(hello);
  EXPECT_TRUE(a.have_client_caps);
  EXPECT_EQ(a.client_caps, hello);
}

TEST(ControlPlane, FormatChangeCarriesNewFormat) {
  proto::Welcome fmt{1, proto::kCodecH264, 1180, 820, 30000, 30, 0};
  auto a = core::HandleControlMessage(proto::FormatChange{fmt});
  EXPECT_TRUE(a.format_change);
  EXPECT_EQ(a.new_format, fmt);
}

TEST(ControlPlane, DisconnectTearsDown) {
  auto a = core::HandleControlMessage(proto::Disconnect{0, "bye"});
  EXPECT_TRUE(a.disconnect);
}

TEST(ControlPlane, HostSentMessagesAreNoOps) {
  // The host sends these; receiving them should do nothing.
  for (const proto::Message& m : {proto::Message{proto::Welcome{}}, proto::Message{proto::Pong{7}},
                                  proto::Message{proto::VideoFrame{}}}) {
    auto a = core::HandleControlMessage(m);
    EXPECT_FALSE(a.force_idr);
    EXPECT_FALSE(a.send_pong);
    EXPECT_FALSE(a.have_client_caps);
    EXPECT_FALSE(a.format_change);
    EXPECT_FALSE(a.disconnect);
  }
}
