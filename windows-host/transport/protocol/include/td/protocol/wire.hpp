// TetherDisplay wire protocol v1 — encode/decode.
// Pure, dependency-free C++20. Implements docs/PROTOCOL.md exactly.
// No platform, GPU, or socket code here: this module is portable and unit-tested on any host.
#ifndef TD_PROTOCOL_WIRE_HPP
#define TD_PROTOCOL_WIRE_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace td::proto {

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t   kMaxPayload = 16u * 1024u * 1024u;  // 16 MiB
inline constexpr std::size_t   kHeaderSize = 7;                    // u32 length + 3 bytes

// Channels (the frame's `channel` byte).
enum class Channel : std::uint8_t { Control = 0, Video = 1 };

// Control-channel message type codes.
namespace ctrl {
inline constexpr std::uint8_t kHello = 0x01;
inline constexpr std::uint8_t kWelcome = 0x02;
inline constexpr std::uint8_t kError = 0x03;
inline constexpr std::uint8_t kKeyframeRequest = 0x10;
inline constexpr std::uint8_t kFormatChange = 0x11;
inline constexpr std::uint8_t kPing = 0x20;
inline constexpr std::uint8_t kPong = 0x21;
inline constexpr std::uint8_t kDisconnect = 0x2F;
}  // namespace ctrl

// Video-channel message type codes.
namespace vid {
inline constexpr std::uint8_t kVideoFrame = 0x01;
}  // namespace vid

// Codec ids and frame types.
inline constexpr std::uint8_t kCodecHEVC = 1;
inline constexpr std::uint8_t kCodecH264 = 2;
inline constexpr std::uint8_t kMaskHEVC = 0x01;
inline constexpr std::uint8_t kMaskH264 = 0x02;
inline constexpr std::uint8_t kFrameDelta = 0;
inline constexpr std::uint8_t kFrameIDR = 1;

// Error codes carried in ERROR / DISCONNECT messages (the on-wire `code` field).
inline constexpr std::uint16_t kErrVersionMismatch = 1;
inline constexpr std::uint16_t kErrUnsupportedCodec = 2;
inline constexpr std::uint16_t kErrInternal = 3;
inline constexpr std::uint16_t kErrProtocolViolation = 4;
inline constexpr std::uint16_t kErrUnauthorized = 5;  // reserved for M5

// Decode failure reasons. Ok == no error.
enum class WireError {
  Ok,
  ShortBuffer,
  OversizedMessage,
  UnknownChannel,
  UnknownType,
  ReservedFlagsSet,
  BadField,
  TrailingBytes,
  UnsupportedVersion,
  NeedMoreData,
};

const char* to_string(WireError e);

// ---- Message structs (decoded form) ----

struct Hello {
  std::uint16_t protocol_version = kProtocolVersion;
  std::uint8_t  codec_mask = 0;
  std::uint16_t max_width = 0;
  std::uint16_t max_height = 0;
  std::uint32_t max_bitrate_kbps = 0;
  std::uint16_t max_fps = 0;
  std::uint8_t  feature_flags = 0;
  bool operator==(const Hello&) const = default;
};

struct Welcome {
  std::uint16_t protocol_version = kProtocolVersion;
  std::uint8_t  codec_id = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint32_t bitrate_kbps = 0;
  std::uint16_t fps = 0;
  std::uint8_t  session_flags = 0;
  bool operator==(const Welcome&) const = default;
};

struct ErrorMsg {
  std::uint16_t code = 0;
  std::string   reason;
  bool operator==(const ErrorMsg&) const = default;
};

struct KeyframeRequest {
  std::uint8_t  reason = 0;
  std::uint32_t last_good_seq = 0;
  bool operator==(const KeyframeRequest&) const = default;
};

struct FormatChange {
  Welcome format;
  bool operator==(const FormatChange&) const = default;
};

struct Ping {
  std::uint64_t token = 0;
  bool operator==(const Ping&) const = default;
};

struct Pong {
  std::uint64_t token = 0;
  bool operator==(const Pong&) const = default;
};

struct Disconnect {
  std::uint16_t code = 0;
  std::string   reason;
  bool operator==(const Disconnect&) const = default;
};

struct VideoFrame {
  std::uint8_t  codec_id = 0;
  std::uint8_t  frame_type = 0;
  std::uint8_t  vflags = 0;
  std::uint64_t pts_us = 0;
  std::uint32_t seq = 0;
  std::vector<std::uint8_t> bitstream;
  bool operator==(const VideoFrame&) const = default;
};

using Message = std::variant<Hello, Welcome, ErrorMsg, KeyframeRequest, FormatChange,
                             Ping, Pong, Disconnect, VideoFrame>;

// A parsed-but-not-yet-interpreted frame: header fields + raw payload bytes.
struct RawFrame {
  Channel      channel = Channel::Control;
  std::uint8_t type = 0;
  std::uint8_t flags = 0;
  std::vector<std::uint8_t> payload;
};

// ---- Encoding (always produces a complete, valid frame) ----
// Preconditions (asserted): payload.size() <= kMaxPayload, and ERROR/DISCONNECT reason strings are
// <= 65535 bytes. Callers construct these messages, so these are contracts (not untrusted-input
// validation); they ensure the encoder can never emit a frame the decoder would reject.

std::vector<std::uint8_t> encode_frame(Channel channel, std::uint8_t type,
                                       std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> encode(const Message& msg);

// ---- Decoding ----

// Interpret a RawFrame's (channel, type, payload) into a typed Message.
// Validates flags==0, version, field consistency, and trailing bytes.
std::optional<Message> decode_message(const RawFrame& frame, WireError& err);

// Incremental frame parser: feed bytes as they arrive, pull complete frames out.
// Buffers partial frames internally; safe against truncated/oversized input.
class FrameParser {
 public:
  void feed(std::span<const std::uint8_t> bytes);

  // Returns the next complete RawFrame, or nullopt. On nullopt, `err` is either NeedMoreData
  // (buffer more bytes and call again) or a hard error (BadField / OversizedMessage /
  // UnknownChannel). A hard error is TERMINAL: the offending bytes are intentionally left in the
  // buffer (not skipped), so the same error repeats until the caller tears the connection down.
  // The wire has no resync marker, so a corrupt length desynchronizes the stream irrecoverably.
  std::optional<RawFrame> next(WireError& err);

  std::size_t buffered() const { return buf_.size() - read_pos_; }

 private:
  std::vector<std::uint8_t> buf_;
  std::size_t read_pos_ = 0;
};

}  // namespace td::proto

#endif  // TD_PROTOCOL_WIRE_HPP
