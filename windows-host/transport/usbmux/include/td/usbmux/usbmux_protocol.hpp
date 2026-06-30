// usbmux wire framing (M2) — the 16-byte header + plist packetization spoken to usbmuxd / Apple Mobile
// Device Service. Pure, portable, dependency-free (no Win32, no sockets): unit-tested anywhere. This is
// a clean-room implementation of the public usbmux protocol — NOT libusbmuxd (GPLv3); see THIRD_PARTY.md.
#ifndef TD_USBMUX_PROTOCOL_HPP
#define TD_USBMUX_PROTOCOL_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace td::usbmux {

inline constexpr std::size_t   kHeaderSize = 16;
inline constexpr std::uint32_t kVersionPlist = 1;  // protocol version: 1 = plist (0 = legacy binary)
inline constexpr std::uint32_t kMessagePlist = 8;  // message type: 8 = TYPE_PLIST

// usbmux Result.Number codes (from a "Result" plist reply).
inline constexpr int kResultOk = 0;
inline constexpr int kResultBadCommand = 1;
inline constexpr int kResultBadDevice = 2;     // requested DeviceID not connected
inline constexpr int kResultConnRefused = 3;   // no app is listening on that device port
inline constexpr int kResultBadVersion = 6;    // (4 and 5 are undefined in the canonical enum)

// The 16-byte little-endian packet header. `length` counts the WHOLE packet, header included
// (length = 16 + plist payload size).
struct MuxHeader {
  std::uint32_t length = 0;
  std::uint32_t version = kVersionPlist;
  std::uint32_t message = kMessagePlist;
  std::uint32_t tag = 0;  // client-chosen request id; the daemon echoes it in the reply
  bool operator==(const MuxHeader&) const = default;
};

std::array<std::uint8_t, kHeaderSize> EncodeHeader(const MuxHeader& h);
std::optional<MuxHeader> DecodeHeader(std::span<const std::uint8_t> bytes);

// Build a complete packet: header (length = 16 + payload, version=plist, message=plist, given tag)
// followed by the plist payload bytes.
std::vector<std::uint8_t> EncodePacket(std::uint32_t tag, std::span<const std::uint8_t> plist_payload);

// usbmux quirk: the Connect plist's PortNumber is the TCP port in BIG-ENDIAN (network order) stored as
// an integer. e.g. port 2345 (0x0929) -> 0x2909 (10505). The daemon ntohs() it back.
inline constexpr std::uint16_t EncodePortBigEndian(std::uint16_t port) {
  return static_cast<std::uint16_t>((port << 8) | (port >> 8));
}

}  // namespace td::usbmux

#endif  // TD_USBMUX_PROTOCOL_HPP
