// usbmux client (M2) — connects to usbmuxd / Apple Mobile Device Service, lists devices, and opens a
// raw byte tunnel to a port an iOS app listens on. Uses portable BSD sockets (Winsock on Windows), so
// it compiles + runs against a mock daemon on Linux/WSL as well as the real service on Windows.
// Clean-room: speaks the wire protocol directly, links NO libusbmuxd/libimobiledevice (GPLv3).
#ifndef TD_USBMUX_CLIENT_HPP
#define TD_USBMUX_CLIENT_HPP

#include "td/usbmux/usbmux_messages.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace td::usbmux {

enum class MuxStatus {
  Ok,
  ConnectFailed,      // couldn't reach the daemon endpoint
  SendFailed,
  RecvFailed,
  Timeout,            // no device/reply within the deadline
  Protocol,           // malformed/unexpected reply
  ResultBadDevice,    // Connect: DeviceID not connected (Number==2)
  ResultConnRefused,  // Connect: nothing listening on that device port (Number==3)
  ResultError,        // Connect: other non-zero Result.Number
};
const char* to_string(MuxStatus s);

// A raw, full-duplex byte pipe to the device port (usbmux socket #2 after a successful Connect).
// Move-only RAII over the underlying socket.
class Tunnel {
 public:
  Tunnel() = default;
  explicit Tunnel(std::int64_t sock) : sock_(sock) {}
  ~Tunnel();
  Tunnel(Tunnel&& o) noexcept : sock_(o.sock_) { o.sock_ = -1; }
  Tunnel& operator=(Tunnel&& o) noexcept;
  Tunnel(const Tunnel&) = delete;
  Tunnel& operator=(const Tunnel&) = delete;

  bool valid() const { return sock_ >= 0; }
  bool SendAll(std::span<const std::uint8_t> data);      // sends everything or returns false
  std::int64_t Recv(std::span<std::uint8_t> buf);        // >0 bytes, 0 = peer closed, <0 = error
  // Shut down both directions to wake a Recv/SendAll blocked on another thread (without closing the fd,
  // which a bare close() does not reliably do on POSIX). Use this to cancel a blocked transfer.
  void Shutdown();
  void Close();

 private:
  std::int64_t sock_ = -1;
};

class UsbmuxClient {
 public:
  // Defaults target Apple Mobile Device Service on Windows; the host/port are overridable so tests can
  // point at a mock daemon. [VERIFY-ON-HW] confirm 27015 against the installed service.
  explicit UsbmuxClient(std::string host = "127.0.0.1", std::uint16_t port = 27015)
      : host_(std::move(host)), port_(port) {}

  // Open a Listen connection and return the first Attached device (blocks up to timeout_ms).
  MuxStatus WaitForDevice(int timeout_ms, AttachedDevice& out);

  // Open a FRESH connection and Connect to `device_port` on `device_id`. On Ok, `out` owns the raw
  // tunnel; recv/send raw application bytes on it (no further usbmux framing).
  MuxStatus Connect(int device_id, std::uint16_t device_port, Tunnel& out);

 private:
  std::string   host_;
  std::uint16_t port_;
};

}  // namespace td::usbmux

#endif  // TD_USBMUX_CLIENT_HPP
