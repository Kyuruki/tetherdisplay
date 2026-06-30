// Adapts a usbmux Tunnel to IByteChannel so StreamingSession can drive it (M4). Header-only, portable.
#ifndef TD_CORE_TUNNEL_CHANNEL_HPP
#define TD_CORE_TUNNEL_CHANNEL_HPP

#include "td/core/byte_channel.hpp"
#include "td/usbmux/usbmux_client.hpp"

namespace td::core {

class TunnelChannel : public IByteChannel {
 public:
  explicit TunnelChannel(usbmux::Tunnel& tunnel) : tunnel_(tunnel) {}
  bool SendAll(std::span<const std::uint8_t> data) override { return tunnel_.SendAll(data); }
  std::int64_t Recv(std::span<std::uint8_t> buf) override { return tunnel_.Recv(buf); }
  void Close() override { tunnel_.Shutdown(); }  // wake a blocked Recv so the session can be torn down

 private:
  usbmux::Tunnel& tunnel_;
};

}  // namespace td::core

#endif  // TD_CORE_TUNNEL_CHANNEL_HPP
