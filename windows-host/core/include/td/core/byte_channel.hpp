// IByteChannel (M4) — the byte pipe the streaming session reads/writes. The live transport is a usbmux
// Tunnel (TunnelChannel adapts it); tests use an in-memory fake. Keeping the session behind this
// interface lets the whole live loop be exercised without a device.
#ifndef TD_CORE_BYTE_CHANNEL_HPP
#define TD_CORE_BYTE_CHANNEL_HPP

#include <cstdint>
#include <span>

namespace td::core {

class IByteChannel {
 public:
  virtual ~IByteChannel() = default;
  // Send all bytes; false on error.
  virtual bool SendAll(std::span<const std::uint8_t> data) = 0;
  // Receive up to buf.size() bytes: >0 = bytes read, 0 = peer closed, <0 = error. May block.
  virtual std::int64_t Recv(std::span<std::uint8_t> buf) = 0;
  // Cancel a blocked Recv/SendAll (e.g. on shutdown) so a reader thread can be joined. Idempotent.
  virtual void Close() = 0;
};

}  // namespace td::core

#endif  // TD_CORE_BYTE_CHANNEL_HPP
