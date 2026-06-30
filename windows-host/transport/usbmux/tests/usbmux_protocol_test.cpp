// Portable unit tests for the usbmux header framing + port encoding.
#include "td/usbmux/usbmux_protocol.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace td::usbmux;

TEST(UsbmuxHeader, RoundTrip) {
  MuxHeader h{16 + 42, kVersionPlist, kMessagePlist, 7};
  auto bytes = EncodeHeader(h);
  auto back = DecodeHeader(bytes);
  ASSERT_TRUE(back);
  EXPECT_EQ(*back, h);
}

TEST(UsbmuxHeader, LittleEndianLayout) {
  // length=16 (0x10), version=1, message=8, tag=0x01020304 -> exact bytes.
  MuxHeader h{16, 1, 8, 0x01020304};
  auto b = EncodeHeader(h);
  const std::array<std::uint8_t, 16> expect{0x10, 0, 0, 0,  1, 0, 0, 0,
                                            0x08, 0, 0, 0,  0x04, 0x03, 0x02, 0x01};
  EXPECT_EQ(b, expect);
}

TEST(UsbmuxHeader, RejectsShortBuffer) {
  std::vector<std::uint8_t> tooShort(15, 0);
  EXPECT_FALSE(DecodeHeader(tooShort));
}

TEST(UsbmuxHeader, RejectsLengthBelowHeader) {
  // length field = 8 (< 16) is malformed.
  std::vector<std::uint8_t> bytes(16, 0);
  bytes[0] = 8;
  EXPECT_FALSE(DecodeHeader(bytes));
}

TEST(UsbmuxPacket, LengthCoversHeaderPlusPayload) {
  std::vector<std::uint8_t> payload{'h', 'e', 'l', 'l', 'o'};
  auto pkt = EncodePacket(/*tag=*/3, payload);
  ASSERT_EQ(pkt.size(), kHeaderSize + payload.size());
  auto h = DecodeHeader(pkt);
  ASSERT_TRUE(h);
  EXPECT_EQ(h->length, kHeaderSize + payload.size());
  EXPECT_EQ(h->version, kVersionPlist);
  EXPECT_EQ(h->message, kMessagePlist);
  EXPECT_EQ(h->tag, 3u);
  EXPECT_EQ(std::vector<std::uint8_t>(pkt.begin() + kHeaderSize, pkt.end()), payload);
}

TEST(UsbmuxPort, BigEndianEncoding) {
  // 2345 = 0x0929 -> byte-swapped 0x2909 = 10505 (the value usbmuxd ntohs() back to 2345).
  EXPECT_EQ(EncodePortBigEndian(2345), 0x2909);
  EXPECT_EQ(EncodePortBigEndian(0x1234), 0x3412);
  EXPECT_EQ(EncodePortBigEndian(80), 0x5000);
}
