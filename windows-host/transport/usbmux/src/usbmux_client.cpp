// usbmux client — portable BSD-socket implementation. See usbmux_client.hpp.
#include "td/usbmux/usbmux_client.hpp"

#include "td/usbmux/usbmux_protocol.hpp"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
namespace {
constexpr sock_t kBad = INVALID_SOCKET;
int close_sock(sock_t s) { return closesocket(s); }
struct WsaInit {
  WsaInit() { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
  ~WsaInit() { WSACleanup(); }
};
WsaInit g_wsa;  // process-wide Winsock init
}  // namespace
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
namespace {
constexpr sock_t kBad = -1;
int close_sock(sock_t s) { return ::close(s); }
}  // namespace
#endif

namespace td::usbmux {
namespace {

sock_t as_sock(std::int64_t s) { return static_cast<sock_t>(s); }

sock_t DialTcp(const std::string& host, std::uint16_t port) {
  sock_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == kBad) return kBad;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) { close_sock(s); return kBad; }
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close_sock(s); return kBad; }
  return s;
}

bool WaitReadable(sock_t s, int timeout_ms) {
  if (timeout_ms < 0) return true;
  fd_set rs;
  FD_ZERO(&rs);
  FD_SET(s, &rs);
  timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  return select(static_cast<int>(s) + 1, &rs, nullptr, nullptr, &tv) > 0;
}

bool SendAllRaw(sock_t s, const std::uint8_t* data, std::size_t n) {
  std::size_t sent = 0;
  while (sent < n) {
    const auto r = send(s, reinterpret_cast<const char*>(data + sent),
                        static_cast<int>(n - sent), 0);
    if (r <= 0) return false;
    sent += static_cast<std::size_t>(r);
  }
  return true;
}

bool RecvExact(sock_t s, std::uint8_t* buf, std::size_t n, int timeout_ms) {
  std::size_t got = 0;
  while (got < n) {
    if (!WaitReadable(s, timeout_ms)) return false;
    const auto r = recv(s, reinterpret_cast<char*>(buf + got), static_cast<int>(n - got), 0);
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

bool SendPacket(sock_t s, std::uint32_t tag, const std::string& plist) {
  auto pkt = EncodePacket(
      tag, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(plist.data()),
                                         plist.size()));
  return SendAllRaw(s, pkt.data(), pkt.size());
}

bool RecvPacket(sock_t s, int timeout_ms, std::string& plist_out) {
  std::uint8_t hb[kHeaderSize];
  if (!RecvExact(s, hb, kHeaderSize, timeout_ms)) return false;
  auto h = DecodeHeader(hb);
  if (!h) return false;
  const std::size_t plen = h->length - kHeaderSize;
  plist_out.resize(plen);
  if (plen == 0) return true;
  return RecvExact(s, reinterpret_cast<std::uint8_t*>(plist_out.data()), plen, timeout_ms);
}

MuxStatus MapResult(int number) {
  switch (number) {
    case kResultBadDevice: return MuxStatus::ResultBadDevice;
    case kResultConnRefused: return MuxStatus::ResultConnRefused;
    default: return MuxStatus::ResultError;
  }
}

}  // namespace

const char* to_string(MuxStatus s) {
  switch (s) {
    case MuxStatus::Ok: return "Ok";
    case MuxStatus::ConnectFailed: return "ConnectFailed";
    case MuxStatus::SendFailed: return "SendFailed";
    case MuxStatus::RecvFailed: return "RecvFailed";
    case MuxStatus::Timeout: return "Timeout";
    case MuxStatus::Protocol: return "Protocol";
    case MuxStatus::ResultBadDevice: return "ResultBadDevice";
    case MuxStatus::ResultConnRefused: return "ResultConnRefused";
    case MuxStatus::ResultError: return "ResultError";
  }
  return "Unknown";
}

Tunnel::~Tunnel() { Close(); }

Tunnel& Tunnel::operator=(Tunnel&& o) noexcept {
  if (this != &o) {
    Close();
    sock_ = o.sock_;
    o.sock_ = -1;
  }
  return *this;
}

void Tunnel::Shutdown() {
  if (sock_ >= 0) {
#if defined(_WIN32)
    ::shutdown(as_sock(sock_), SD_BOTH);
#else
    ::shutdown(as_sock(sock_), SHUT_RDWR);
#endif
  }
}

void Tunnel::Close() {
  if (sock_ >= 0) {
    close_sock(as_sock(sock_));
    sock_ = -1;
  }
}

bool Tunnel::SendAll(std::span<const std::uint8_t> data) {
  if (!valid()) return false;
  return SendAllRaw(as_sock(sock_), data.data(), data.size());
}

std::int64_t Tunnel::Recv(std::span<std::uint8_t> buf) {
  if (!valid()) return -1;
  return recv(as_sock(sock_), reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
}

MuxStatus UsbmuxClient::WaitForDevice(int timeout_ms, AttachedDevice& out) {
  sock_t s = DialTcp(host_, port_);
  if (s == kBad) return MuxStatus::ConnectFailed;
  if (!SendPacket(s, /*tag=*/1, BuildListenPlist())) { close_sock(s); return MuxStatus::SendFailed; }

  for (int i = 0; i < 64; ++i) {  // skip Detached/other events; bounded so we can't loop forever
    std::string plist;
    if (!RecvPacket(s, timeout_ms, plist)) { close_sock(s); return MuxStatus::Timeout; }
    if (auto dev = ParseAttached(plist)) {
      out = *dev;
      close_sock(s);  // we only need the DeviceID for M2; a real session keeps this open for events
      return MuxStatus::Ok;
    }
  }
  close_sock(s);
  return MuxStatus::Timeout;
}

MuxStatus UsbmuxClient::Connect(int device_id, std::uint16_t device_port, Tunnel& out) {
  sock_t s = DialTcp(host_, port_);  // Connect REQUIRES its own socket, separate from Listen
  if (s == kBad) return MuxStatus::ConnectFailed;
  if (!SendPacket(s, /*tag=*/2, BuildConnectPlist(device_id, device_port))) {
    close_sock(s);
    return MuxStatus::SendFailed;
  }
  std::string plist;
  if (!RecvPacket(s, /*timeout_ms=*/5000, plist)) { close_sock(s); return MuxStatus::RecvFailed; }
  auto number = ParseResultNumber(plist);
  if (!number) { close_sock(s); return MuxStatus::Protocol; }
  if (*number != kResultOk) { close_sock(s); return MapResult(*number); }
  out = Tunnel(static_cast<std::int64_t>(s));  // socket is now the raw tunnel; Tunnel owns it
  return MuxStatus::Ok;
}

}  // namespace td::usbmux
