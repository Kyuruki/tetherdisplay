// Integration test: drive UsbmuxClient against an in-process MOCK usbmux daemon over real loopback
// sockets. Validates the whole flow (Listen -> Attached, separate Connect -> Result{0} -> raw tunnel)
// without any Apple software or device. POSIX-only (the live Windows path uses the real service + the
// capture/encode sandboxes); the portable framing/plist tests cover both platforms.
#include "td/usbmux/usbmux_client.hpp"
#include "td/usbmux/usbmux_messages.hpp"
#include "td/usbmux/usbmux_protocol.hpp"

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace td::usbmux;

namespace {

// Minimal mock usbmuxd: serves one Listen connection (replies Attached), then one Connect connection
// (replies Result{0}, then echoes raw bytes).
class MockDaemon {
 public:
  bool Start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;  // ephemeral
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
    socklen_t len = sizeof(a);
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
    if (listen(listen_fd_, 4) != 0) return false;
    thread_ = std::thread([this] { Run(); });
    return true;
  }

  std::uint16_t port() const { return port_; }

  void Stop() {
    stop_ = true;
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }  // unblocks a pending accept()
    if (thread_.joinable()) thread_.join();
  }

  ~MockDaemon() { Stop(); }

  // The Connect request the mock received (read after Stop()/join for a clean happens-before).
  const std::string& last_connect() const { return last_connect_; }

 private:
  static bool RecvPkt(int fd, std::string& plist) {
    std::uint8_t hb[kHeaderSize];
    std::size_t got = 0;
    while (got < kHeaderSize) {
      auto r = recv(fd, reinterpret_cast<char*>(hb) + got, kHeaderSize - got, 0);
      if (r <= 0) return false;
      got += static_cast<std::size_t>(r);
    }
    auto h = DecodeHeader(hb);
    if (!h) return false;
    const std::size_t n = h->length - kHeaderSize;
    plist.resize(n);
    got = 0;
    while (got < n) {
      auto r = recv(fd, plist.data() + got, n - got, 0);
      if (r <= 0) return false;
      got += static_cast<std::size_t>(r);
    }
    return true;
  }

  static void SendPkt(int fd, std::uint32_t tag, const std::string& plist) {
    auto pkt = EncodePacket(
        tag, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(plist.data()),
                                           plist.size()));
    std::size_t s = 0;
    while (s < pkt.size()) {
      auto r = send(fd, reinterpret_cast<char*>(pkt.data()) + s, pkt.size() - s, 0);
      if (r <= 0) return;
      s += static_cast<std::size_t>(r);
    }
  }

  void Run() {
    // Connection 1: Listen -> Attached event.
    int c1 = accept(listen_fd_, nullptr, nullptr);
    if (c1 >= 0) {
      std::string req;
      RecvPkt(c1, req);
      const std::string attached =
          "<plist version=\"1.0\"><dict>"
          "<key>MessageType</key><string>Attached</string>"
          "<key>DeviceID</key><integer>7</integer>"
          "<key>Properties</key><dict>"
          "<key>ConnectionType</key><string>USB</string>"
          "<key>SerialNumber</key><string>MOCKUDID123</string>"
          "</dict></dict></plist>";
      SendPkt(c1, 1, attached);
      ::close(c1);  // client closes its side after reading Attached
    }
    // Connection 2: Connect -> Result{0} -> raw echo.
    int c2 = accept(listen_fd_, nullptr, nullptr);
    if (c2 >= 0) {
      std::string req;
      RecvPkt(c2, req);
      last_connect_ = req;  // capture for the test to assert the Connect plist after join
      const std::string result =
          "<plist version=\"1.0\"><dict>"
          "<key>MessageType</key><string>Result</string>"
          "<key>Number</key><integer>0</integer></dict></plist>";
      SendPkt(c2, 2, result);
      char buf[256];
      for (;;) {
        auto r = recv(c2, buf, sizeof(buf), 0);
        if (r <= 0) break;  // client closed the tunnel
        std::size_t s = 0;
        while (static_cast<ssize_t>(s) < r) {
          auto w = send(c2, buf + s, static_cast<std::size_t>(r) - s, 0);
          if (w <= 0) break;
          s += static_cast<std::size_t>(w);
        }
      }
      ::close(c2);
    }
  }

  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::string last_connect_;  // written by the worker before sending Result; read after join
};

}  // namespace

TEST(UsbmuxClientMock, FullListenConnectTunnelFlow) {
  MockDaemon mock;
  ASSERT_TRUE(mock.Start());
  UsbmuxClient client("127.0.0.1", mock.port());

  AttachedDevice dev;
  ASSERT_EQ(client.WaitForDevice(2000, dev), MuxStatus::Ok);
  EXPECT_EQ(dev.device_id, 7);
  EXPECT_EQ(dev.serial, "MOCKUDID123");
  EXPECT_EQ(dev.connection_type, "USB");

  Tunnel tunnel;
  ASSERT_EQ(client.Connect(dev.device_id, /*device_port=*/2345, tunnel), MuxStatus::Ok);
  ASSERT_TRUE(tunnel.valid());

  const std::string msg = "hello-over-usb";
  std::vector<std::uint8_t> tx(msg.begin(), msg.end());
  ASSERT_TRUE(tunnel.SendAll(tx));

  std::vector<std::uint8_t> rx(tx.size());
  std::size_t got = 0;
  while (got < rx.size()) {
    auto r = tunnel.Recv(std::span<std::uint8_t>(rx.data() + got, rx.size() - got));
    ASSERT_GT(r, 0);
    got += static_cast<std::size_t>(r);
  }
  EXPECT_EQ(rx, tx);

  tunnel.Close();  // unblock the mock's echo recv (tunnel is declared after mock, so ~Tunnel runs
                   // first on any early ASSERT, closing c2 before ~MockDaemon joins)
  mock.Stop();     // join -> happens-before for reading the captured request

  // Keep the e2e flow non-vacuous: assert the daemon actually received our Connect plist with the
  // right DeviceID and the big-endian PortNumber (2345 -> htons -> 10505).
  EXPECT_NE(mock.last_connect().find("<key>DeviceID</key><integer>7</integer>"), std::string::npos);
  EXPECT_NE(mock.last_connect().find("<key>PortNumber</key><integer>10505</integer>"), std::string::npos);
}

#endif  // !_WIN32
