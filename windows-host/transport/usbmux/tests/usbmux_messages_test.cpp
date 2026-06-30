// Portable unit tests for the usbmux plist builders + parsers.
#include "td/usbmux/usbmux_messages.hpp"
#include "td/usbmux/usbmux_protocol.hpp"

#include <gtest/gtest.h>

using namespace td::usbmux;

namespace {
bool contains(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}
}  // namespace

TEST(BuildListen, HasMessageTypeAndBoilerplate) {
  auto p = BuildListenPlist();
  EXPECT_TRUE(contains(p, "<key>MessageType</key><string>Listen</string>"));
  EXPECT_TRUE(contains(p, "<key>kLibUSBMuxVersion</key><integer>3</integer>"));
  EXPECT_TRUE(contains(p, "<key>ProgName</key><string>TetherDisplay</string>"));
}

TEST(BuildConnect, HasDeviceIdAndBigEndianPort) {
  auto p = BuildConnectPlist(/*device_id=*/7, /*device_port=*/2345);
  EXPECT_TRUE(contains(p, "<key>MessageType</key><string>Connect</string>"));
  EXPECT_TRUE(contains(p, "<key>DeviceID</key><integer>7</integer>"));
  // 2345 -> htons -> 0x2909 = 10505
  EXPECT_TRUE(contains(p, "<key>PortNumber</key><integer>10505</integer>"));
}

TEST(PlistGet, StringAndInteger) {
  std::string xml =
      "<dict><key>MessageType</key><string>Result</string>"
      "<key>Number</key><integer>0</integer></dict>";
  EXPECT_EQ(PlistGetString(xml, "MessageType"), std::optional<std::string>("Result"));
  EXPECT_EQ(PlistGetInteger(xml, "Number"), std::optional<long long>(0));
  EXPECT_FALSE(PlistGetString(xml, "Missing"));
  EXPECT_FALSE(PlistGetInteger(xml, "MessageType"));  // value is a string, not an integer
}

TEST(ParseResult, ExtractsNumber) {
  EXPECT_EQ(ParseResultNumber("<dict><key>MessageType</key><string>Result</string>"
                              "<key>Number</key><integer>0</integer></dict>"),
            std::optional<int>(kResultOk));
  EXPECT_EQ(ParseResultNumber("<dict><key>Number</key><integer>3</integer></dict>"),
            std::optional<int>(kResultConnRefused));
}

TEST(ParseAttached, ExtractsDeviceFields) {
  // Mirrors the real Attached event: DeviceID at the top, serial/type nested under Properties.
  std::string xml =
      "<dict>"
      "<key>MessageType</key><string>Attached</string>"
      "<key>DeviceID</key><integer>7</integer>"
      "<key>Properties</key><dict>"
      "<key>ConnectionType</key><string>USB</string>"
      "<key>SerialNumber</key><string>00008030-ABCDEF0123456789</string>"
      "</dict></dict>";
  auto d = ParseAttached(xml);
  ASSERT_TRUE(d);
  EXPECT_EQ(d->device_id, 7);
  EXPECT_EQ(d->connection_type, "USB");
  EXPECT_EQ(d->serial, "00008030-ABCDEF0123456789");
}

TEST(ParseAttached, RejectsNonAttached) {
  EXPECT_FALSE(ParseAttached("<dict><key>MessageType</key><string>Detached</string>"
                             "<key>DeviceID</key><integer>7</integer></dict>"));
}
