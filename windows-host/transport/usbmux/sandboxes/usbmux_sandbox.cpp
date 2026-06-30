// M2 host gate (R5) — connects to Apple Mobile Device Service, finds the iPad, opens a usbmux tunnel
// to the app's listening port, and streams deterministic bytes with a rolling checksum. The iPad gate
// app shows its received byte count + checksum; they must match this program's printed totals.
//
//   td_usbmux_sandbox [device_port] [kilobytes]      (defaults: 2345, 64)
//
// Runs on Windows against the real service; also runs on Linux/WSL if pointed at a mock (the unit test
// already exercises that path). The agent cannot run the live device path.
#include "td/usbmux/usbmux_client.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace td::usbmux;

namespace {
// FNV-1a 32-bit — the iPad gate app computes the same over the received bytes.
std::uint32_t fnv1a(std::uint32_t h, const std::uint8_t* p, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 0x01000193u;
  }
  return h;
}
}  // namespace

int main(int argc, char** argv) {
  const std::uint16_t device_port = (argc >= 2) ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 2345;
  const int kilobytes = (argc >= 3) ? std::atoi(argv[2]) : 64;

  UsbmuxClient client;  // 127.0.0.1:27015 (Apple Mobile Device Service)
  std::printf("Waiting for an attached device...\n");
  AttachedDevice dev;
  if (auto st = client.WaitForDevice(/*timeout_ms=*/10000, dev); st != MuxStatus::Ok) {
    std::fprintf(stderr, "WaitForDevice failed: %s\n", to_string(st));
    std::fprintf(stderr, "  -> Is Apple Mobile Device Service running and the iPad plugged in/trusted?\n");
    return 1;
  }
  std::printf("Device %d attached (%s, %s)\n", dev.device_id, dev.connection_type.c_str(),
              dev.serial.c_str());

  Tunnel tunnel;
  if (auto st = client.Connect(dev.device_id, device_port, tunnel); st != MuxStatus::Ok) {
    std::fprintf(stderr, "Connect to device port %u failed: %s\n", device_port, to_string(st));
    if (st == MuxStatus::ResultConnRefused)
      std::fprintf(stderr, "  -> The iPad gate app isn't listening on port %u. Launch it first.\n",
                   device_port);
    return 1;
  }
  std::printf("Tunnel open to device port %u. Streaming %d KiB...\n", device_port, kilobytes);

  std::uint32_t checksum = 0x811C9DC5u;  // FNV offset basis
  std::uint64_t total = 0;
  std::vector<std::uint8_t> chunk(1024);
  for (int k = 0; k < kilobytes; ++k) {
    for (std::size_t i = 0; i < chunk.size(); ++i)
      chunk[i] = static_cast<std::uint8_t>((total + i) * 31u + 7u);  // deterministic filler; the device
                                                                     // checksums what it RECEIVES
    if (!tunnel.SendAll(chunk)) {
      std::fprintf(stderr, "SendAll failed after %llu bytes\n", (unsigned long long)total);
      return 1;
    }
    checksum = fnv1a(checksum, chunk.data(), chunk.size());
    total += chunk.size();
  }

  std::printf("Sent %llu bytes, FNV-1a checksum = 0x%08X\n", (unsigned long long)total, checksum);
  std::printf(">>> The iPad gate app should show the SAME byte count and checksum.\n");
  return 0;
}
