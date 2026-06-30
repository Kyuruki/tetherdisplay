// Unit + golden-vector tests for the v1 wire protocol.
// Golden vectors live in protocol/test-vectors/*.hex; the path is injected as TD_VECTORS_DIR.
#include "td/protocol/wire.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace td::proto;

namespace {

#ifndef TD_VECTORS_DIR
#error "TD_VECTORS_DIR must be defined (path to protocol/test-vectors)"
#endif

std::vector<std::uint8_t> from_hex(const std::string& hex) {
  std::vector<std::uint8_t> out;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int hi = -1;
  for (char c : hex) {
    int v = nib(c);
    if (v < 0) continue;  // skip whitespace/newlines
    if (hi < 0) {
      hi = v;
    } else {
      out.push_back(static_cast<std::uint8_t>((hi << 4) | v));
      hi = -1;
    }
  }
  EXPECT_LT(hi, 0) << "odd number of hex digits";
  return out;
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
  static const char* d = "0123456789ABCDEF";
  std::string s;
  for (auto b : bytes) { s.push_back(d[b >> 4]); s.push_back(d[b & 0xF]); }
  return s;
}

std::vector<std::uint8_t> load_vector(const std::string& name) {
  std::ifstream f(std::string(TD_VECTORS_DIR) + "/" + name);
  EXPECT_TRUE(f.good()) << "cannot open vector " << name;
  std::stringstream ss;
  ss << f.rdbuf();
  return from_hex(ss.str());
}

// Parse exactly one frame out of a complete byte buffer and decode it.
std::optional<Message> decode_one(const std::vector<std::uint8_t>& bytes, WireError& err) {
  FrameParser p;
  p.feed(bytes);
  auto raw = p.next(err);
  if (!raw) return std::nullopt;
  return decode_message(*raw, err);
}

// Assert that a message both decodes from the golden bytes AND re-encodes to them.
void expect_roundtrip(const std::string& vector_name, const Message& expected) {
  auto golden = load_vector(vector_name);

  WireError err = WireError::Ok;
  auto decoded = decode_one(golden, err);
  ASSERT_TRUE(decoded.has_value()) << vector_name << " decode failed: " << to_string(err);
  EXPECT_EQ(*decoded, expected) << vector_name << " decoded fields differ from expected";

  auto reencoded = encode(expected);
  EXPECT_EQ(to_hex(reencoded), to_hex(golden))
      << vector_name << " re-encode does not match golden bytes";
}

}  // namespace

// ---- Golden vectors: every message type round-trips against the committed fixtures ----

TEST(Vectors, Ping) { expect_roundtrip("ping.hex", Ping{0x0102030405060708ull}); }
TEST(Vectors, Pong) { expect_roundtrip("pong.hex", Pong{0x0102030405060708ull}); }

TEST(Vectors, KeyframeRequest) {
  expect_roundtrip("keyframe_request.hex", KeyframeRequest{/*reason=*/1, /*last_good_seq=*/255});
}

TEST(Vectors, Hello) {
  expect_roundtrip("hello.hex",
                   Hello{1, kMaskHEVC | kMaskH264, 2360, 1640, 80000, 60, 0});
}

TEST(Vectors, Welcome) {
  expect_roundtrip("welcome.hex", Welcome{1, kCodecHEVC, 2360, 1640, 60000, 60, 0});
}

TEST(Vectors, FormatChange) {
  expect_roundtrip("format_change.hex",
                   FormatChange{Welcome{1, kCodecH264, 1180, 820, 30000, 30, 0}});
}

TEST(Vectors, Error) {
  expect_roundtrip("error.hex", ErrorMsg{kErrVersionMismatch, "bad version"});
}

TEST(Vectors, Disconnect) {
  expect_roundtrip("disconnect.hex", Disconnect{/*code=*/0, "bye"});
}

TEST(Vectors, VideoFrameIDR) {
  VideoFrame f{kCodecHEVC, kFrameIDR, 0, 1000000, 1,
               {0x00, 0x00, 0x00, 0x01, 0x67, 0xAA, 0xBB}};
  expect_roundtrip("video_frame_idr.hex", f);
}

// ---- Programmatic round-trips (no fixture needed) ----

TEST(RoundTrip, EmptyBitstreamVideoFrame) {
  VideoFrame f{kCodecH264, kFrameDelta, 0, 42, 7, {}};
  WireError err;
  auto out = decode_one(encode(f), err);
  ASSERT_TRUE(out) << to_string(err);
  EXPECT_EQ(std::get<VideoFrame>(*out), f);
}

TEST(RoundTrip, ErrorWithEmptyReason) {
  ErrorMsg e{kErrInternal, ""};
  WireError err;
  auto out = decode_one(encode(e), err);
  ASSERT_TRUE(out) << to_string(err);
  EXPECT_EQ(std::get<ErrorMsg>(*out), e);
}

// ---- Negative / robustness tests: malformed input must fail cleanly ----

TEST(Errors, ReservedFlagsRejected) {
  auto bytes = encode(Ping{1});
  bytes[6] = 0x01;  // set a reserved flag
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::ReservedFlagsSet);
}

TEST(Errors, UnknownControlTypeRejected) {
  auto bytes = encode_frame(Channel::Control, 0x7E, {});
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::UnknownType);
}

TEST(Errors, UnknownChannelRejected) {
  std::vector<std::uint8_t> payload(8, 0);
  auto bytes = encode_frame(Channel::Control, ctrl::kPing, payload);
  bytes[4] = 0x09;  // channel 9 does not exist
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::UnknownChannel);
}

TEST(Errors, ShortPayloadRejected) {
  // A PING frame claiming an 8-byte token but only 4 present.
  auto bytes = encode_frame(Channel::Control, ctrl::kPing, std::vector<std::uint8_t>(4, 0));
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::ShortBuffer);
}

TEST(Errors, TrailingBytesRejected) {
  // A PING with 9 payload bytes (one too many) must be rejected, not silently truncated.
  auto bytes = encode_frame(Channel::Control, ctrl::kPing, std::vector<std::uint8_t>(9, 0));
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::TrailingBytes);
}

TEST(Errors, ErrorReasonLenMismatchRejected) {
  // reason_len says 5 but only 3 reason bytes follow.
  std::vector<std::uint8_t> payload = {0x01, 0x00, 0x05, 0x00, 'a', 'b', 'c'};
  auto bytes = encode_frame(Channel::Control, ctrl::kError, payload);
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::BadField);
}

TEST(Errors, OversizedLengthRejected) {
  // Hand-craft a header whose declared payload exceeds kMaxPayload.
  std::vector<std::uint8_t> bytes(16, 0);
  std::uint32_t length = static_cast<std::uint32_t>(kMaxPayload + 3 + 1);
  for (int i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>((length >> (8 * i)) & 0xFF);
  bytes[4] = 0;  // control
  bytes[5] = ctrl::kPing;
  FrameParser p;
  p.feed(bytes);
  WireError err;
  auto raw = p.next(err);
  EXPECT_FALSE(raw);
  EXPECT_EQ(err, WireError::OversizedMessage);
}

// ---- Incremental parser: byte-at-a-time and multi-frame buffers ----

TEST(Parser, ByteAtATimeYieldsFrameOnlyWhenComplete) {
  auto bytes = encode(Hello{1, kMaskHEVC, 2360, 1640, 80000, 60, 0});
  FrameParser p;
  WireError err;
  for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
    p.feed(std::span<const std::uint8_t>(&bytes[i], 1));
    auto r = p.next(err);
    EXPECT_FALSE(r) << "frame yielded too early at byte " << i;
    EXPECT_EQ(err, WireError::NeedMoreData);
  }
  p.feed(std::span<const std::uint8_t>(&bytes.back(), 1));
  auto r = p.next(err);
  ASSERT_TRUE(r);
  EXPECT_EQ(r->channel, Channel::Control);
  EXPECT_EQ(r->type, ctrl::kHello);
}

TEST(Parser, TwoFramesInOneBuffer) {
  auto a = encode(Ping{1});
  auto b = encode(Pong{2});
  std::vector<std::uint8_t> both = a;
  both.insert(both.end(), b.begin(), b.end());

  FrameParser p;
  p.feed(both);
  WireError err;

  auto r1 = p.next(err);
  ASSERT_TRUE(r1);
  EXPECT_EQ(std::get<Ping>(*decode_message(*r1, err)), Ping{1});

  auto r2 = p.next(err);
  ASSERT_TRUE(r2);
  EXPECT_EQ(std::get<Pong>(*decode_message(*r2, err)), Pong{2});

  auto r3 = p.next(err);
  EXPECT_FALSE(r3);
  EXPECT_EQ(err, WireError::NeedMoreData);
}

// ---- Version negotiation: a peer on a different protocol version is rejected, never downgraded ----
// (PROTOCOL.md §4.5: "fails loudly with a typed error — never guess or downgrade silently".)

TEST(Version, HelloMismatchRejected) {
  for (std::uint16_t v : {std::uint16_t{0}, std::uint16_t{2}, std::uint16_t{255}}) {
    Hello h{v, kMaskHEVC, 2360, 1640, 80000, 60, 0};
    WireError err;
    auto out = decode_one(encode(h), err);
    EXPECT_FALSE(out) << "HELLO version " << v << " must not decode";
    EXPECT_EQ(err, WireError::UnsupportedVersion) << "HELLO version " << v;
  }
}

TEST(Version, WelcomeMismatchRejected) {
  for (std::uint16_t v : {std::uint16_t{0}, std::uint16_t{2}, std::uint16_t{255}}) {
    Welcome w{v, kCodecHEVC, 2360, 1640, 60000, 60, 0};
    WireError err;
    auto out = decode_one(encode(w), err);
    EXPECT_FALSE(out) << "WELCOME version " << v;
    EXPECT_EQ(err, WireError::UnsupportedVersion) << "WELCOME version " << v;
  }
}

TEST(Version, FormatChangeMismatchRejected) {
  for (std::uint16_t v : {std::uint16_t{0}, std::uint16_t{2}, std::uint16_t{255}}) {
    FormatChange fc{Welcome{v, kCodecH264, 1180, 820, 30000, 30, 0}};
    WireError err;
    auto out = decode_one(encode(fc), err);
    EXPECT_FALSE(out) << "FORMAT_CHANGE version " << v;
    EXPECT_EQ(err, WireError::UnsupportedVersion) << "FORMAT_CHANGE version " << v;
  }
}

// ---- Parser: a sub-minimum length is a typed BadField, not a silent underflow ----

TEST(Errors, LengthBelowMinimumRejected) {
  for (std::uint32_t bad : {std::uint32_t{0}, std::uint32_t{1}, std::uint32_t{2}}) {
    std::vector<std::uint8_t> hdr(4);
    for (int i = 0; i < 4; ++i) hdr[i] = static_cast<std::uint8_t>((bad >> (8 * i)) & 0xFF);
    FrameParser p;
    p.feed(hdr);
    WireError err;
    auto r = p.next(err);
    EXPECT_FALSE(r) << "length " << bad;
    EXPECT_EQ(err, WireError::BadField) << "length " << bad;
  }
}

// ---- VIDEO_FRAME decode edges: truncated header, exact boundary, unknown video type ----

TEST(Errors, VideoFrameTruncatedHeaderRejected) {
  // 10-byte payload is shorter than the 15-byte VIDEO_FRAME header.
  auto bytes = encode_frame(Channel::Video, vid::kVideoFrame, std::vector<std::uint8_t>(10, 0));
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::ShortBuffer);
}

TEST(RoundTrip, VideoFrameHeaderExactBoundaryEmptyBitstream) {
  // Exactly the 15-byte header, zero bitstream bytes -> valid frame with empty bitstream.
  VideoFrame f{kCodecHEVC, kFrameDelta, 0, 0, 0, {}};
  WireError err;
  auto out = decode_one(encode(f), err);
  ASSERT_TRUE(out) << to_string(err);
  EXPECT_TRUE(std::get<VideoFrame>(*out).bitstream.empty());
}

TEST(Errors, UnknownVideoTypeRejected) {
  auto bytes = encode_frame(Channel::Video, 0x02, {});  // 0x02 is not VIDEO_FRAME
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::UnknownType);
}

// ---- TrailingBytes on a fixed-size control message other than PING ----

TEST(Errors, KeyframeRequestTrailingBytesRejected) {
  // KEYFRAME_REQUEST is exactly 5 bytes; a 6-byte payload must be rejected, not truncated.
  auto bytes = encode_frame(Channel::Control, ctrl::kKeyframeRequest, std::vector<std::uint8_t>(6, 0));
  WireError err;
  auto out = decode_one(bytes, err);
  EXPECT_FALSE(out);
  EXPECT_EQ(err, WireError::TrailingBytes);
}

// ---- Parser compaction: a frame past the 64 KiB threshold plus a trailing partial stays bounded ----

TEST(Parser, CompactionAfterLargeFrameKeepsStreamBounded) {
  VideoFrame big{kCodecHEVC, kFrameIDR, 0, 7, 1, std::vector<std::uint8_t>(100 * 1024, 0xCD)};
  auto a = encode(big);
  auto b = encode(Ping{0xABCDEF});

  FrameParser p;
  p.feed(a);
  p.feed(std::span<const std::uint8_t>(b.data(), 2));  // 2 bytes of the next frame trail behind
  WireError err;

  auto r1 = p.next(err);
  ASSERT_TRUE(r1) << to_string(err);
  EXPECT_EQ(std::get<VideoFrame>(*decode_message(*r1, err)), big);

  // read_pos_ is now >64 KiB with 2 leftover bytes; the next feed must compact and still decode.
  p.feed(std::span<const std::uint8_t>(b.data() + 2, b.size() - 2));
  auto r2 = p.next(err);
  ASSERT_TRUE(r2) << to_string(err);
  EXPECT_EQ(std::get<Ping>(*decode_message(*r2, err)), Ping{0xABCDEF});
  EXPECT_EQ(p.buffered(), 0u);
}
