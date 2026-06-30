// M0 loopback: runs the host's framing and a mock client's framing in ONE process.
// No device, no driver, no sockets. Proves a synthetic frame survives encode -> parse ->
// decode end-to-end, and that control/video share the byte stream correctly.
//
// Exit code 0 = gate passes; non-zero = failure (with a diagnostic on stderr).
#include "td/protocol/wire.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace td::proto;

namespace {

int g_failures = 0;
void check(bool cond, const char* what) {
  if (!cond) {
    std::fprintf(stderr, "  FAIL: %s\n", what);
    ++g_failures;
  }
}

// A synthetic 64 KiB "encoded frame" with a recognizable pattern (NOT real pixels —
// the bitstream is opaque to the protocol; this just exercises the byte path).
std::vector<std::uint8_t> synthetic_bitstream(std::size_t n) {
  std::vector<std::uint8_t> b(n);
  for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
  return b;
}

}  // namespace

int main() {
  std::printf("TetherDisplay M0 loopback (in-process host <-> mock client)\n");

  // --- Host side: build the session opening + one video frame, serialize to one byte stream. ---
  const Hello host_hello{1, kMaskHEVC | kMaskH264, 2360, 1640, 80000, 60, 0};
  const Welcome welcome{1, kCodecHEVC, 2360, 1640, 60000, 60, 0};
  VideoFrame frame{kCodecHEVC, kFrameIDR, 0, /*pts_us=*/1'000'000, /*seq=*/1,
                   synthetic_bitstream(64 * 1024)};

  std::vector<std::uint8_t> wire;
  auto append = [&](const std::vector<std::uint8_t>& f) {
    wire.insert(wire.end(), f.begin(), f.end());
  };
  append(encode(host_hello));
  append(encode(welcome));
  append(encode(frame));
  std::printf("  host emitted %zu bytes across 3 frames\n", wire.size());

  // --- Mock client side: feed the stream into the parser in awkward 1000-byte chunks. ---
  FrameParser parser;
  std::vector<Message> received;
  WireError err = WireError::Ok;
  for (std::size_t off = 0; off < wire.size(); off += 1000) {
    const std::size_t n = std::min<std::size_t>(1000, wire.size() - off);
    parser.feed(std::span<const std::uint8_t>(wire.data() + off, n));
    while (true) {
      auto raw = parser.next(err);
      if (!raw) {
        check(err == WireError::NeedMoreData, "parser returned a hard error");
        break;
      }
      auto msg = decode_message(*raw, err);
      check(msg.has_value(), "decode_message failed on a framed message");
      if (msg) received.push_back(std::move(*msg));
    }
  }

  // --- Verify the client saw exactly what the host sent, in order. ---
  check(received.size() == 3, "expected 3 messages");
  if (received.size() == 3) {
    check(std::holds_alternative<Hello>(received[0]) &&
              std::get<Hello>(received[0]) == host_hello,
          "HELLO mismatch");
    check(std::holds_alternative<Welcome>(received[1]) &&
              std::get<Welcome>(received[1]) == welcome,
          "WELCOME mismatch");
    check(std::holds_alternative<VideoFrame>(received[2]) &&
              std::get<VideoFrame>(received[2]) == frame,
          "VIDEO_FRAME mismatch (header or 64 KiB bitstream differs)");
  }

  if (g_failures == 0) {
    std::printf("  OK: synthetic frame streamed end-to-end, all fields preserved.\n");
    return 0;
  }
  std::fprintf(stderr, "  %d check(s) failed.\n", g_failures);
  return 1;
}
