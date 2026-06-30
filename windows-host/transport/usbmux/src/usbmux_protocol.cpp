// usbmux header framing — portable implementation. See usbmux_protocol.hpp.
#include "td/usbmux/usbmux_protocol.hpp"

namespace td::usbmux {
namespace {

void put_u32_le(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFF);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

std::uint32_t get_u32_le(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

std::array<std::uint8_t, kHeaderSize> EncodeHeader(const MuxHeader& h) {
  std::array<std::uint8_t, kHeaderSize> out{};
  put_u32_le(out.data() + 0, h.length);
  put_u32_le(out.data() + 4, h.version);
  put_u32_le(out.data() + 8, h.message);
  put_u32_le(out.data() + 12, h.tag);
  return out;
}

std::optional<MuxHeader> DecodeHeader(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kHeaderSize) return std::nullopt;
  MuxHeader h;
  h.length = get_u32_le(bytes.data() + 0);
  h.version = get_u32_le(bytes.data() + 4);
  h.message = get_u32_le(bytes.data() + 8);
  h.tag = get_u32_le(bytes.data() + 12);
  if (h.length < kHeaderSize) return std::nullopt;  // length must at least cover the header
  return h;
}

std::vector<std::uint8_t> EncodePacket(std::uint32_t tag, std::span<const std::uint8_t> plist_payload) {
  MuxHeader h;
  h.length = static_cast<std::uint32_t>(kHeaderSize + plist_payload.size());
  h.version = kVersionPlist;
  h.message = kMessagePlist;
  h.tag = tag;
  const auto header = EncodeHeader(h);

  std::vector<std::uint8_t> out;
  out.reserve(h.length);
  out.insert(out.end(), header.begin(), header.end());
  out.insert(out.end(), plist_payload.begin(), plist_payload.end());
  return out;
}

}  // namespace td::usbmux
