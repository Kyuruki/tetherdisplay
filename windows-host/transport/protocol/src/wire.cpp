// TetherDisplay wire protocol v1 — implementation. See docs/PROTOCOL.md.
#include "td/protocol/wire.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace td::proto {

const char* to_string(WireError e) {
  switch (e) {
    case WireError::Ok: return "Ok";
    case WireError::ShortBuffer: return "ShortBuffer";
    case WireError::OversizedMessage: return "OversizedMessage";
    case WireError::UnknownChannel: return "UnknownChannel";
    case WireError::UnknownType: return "UnknownType";
    case WireError::ReservedFlagsSet: return "ReservedFlagsSet";
    case WireError::BadField: return "BadField";
    case WireError::TrailingBytes: return "TrailingBytes";
    case WireError::UnsupportedVersion: return "UnsupportedVersion";
    case WireError::NeedMoreData: return "NeedMoreData";
  }
  return "Unknown";
}

namespace {

// Little-endian writer onto a byte vector.
class ByteWriter {
 public:
  explicit ByteWriter(std::vector<std::uint8_t>& out) : out_(out) {}
  void u8(std::uint8_t v) { out_.push_back(v); }
  void u16(std::uint16_t v) {
    out_.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  }
  void u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
  }
  void u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
  }
  void bytes(std::span<const std::uint8_t> b) { out_.insert(out_.end(), b.begin(), b.end()); }

 private:
  std::vector<std::uint8_t>& out_;
};

// Bounds-checked little-endian reader. The first failed read latches `error`;
// all subsequent reads are no-ops returning 0, so callers can read then check once.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

  std::uint8_t u8() {
    if (!ensure(1)) return 0;
    return data_[pos_++];
  }
  std::uint16_t u16() {
    if (!ensure(2)) return 0;
    std::uint16_t v = static_cast<std::uint16_t>(data_[pos_]) |
                      (static_cast<std::uint16_t>(data_[pos_ + 1]) << 8);
    pos_ += 2;
    return v;
  }
  std::uint32_t u32() {
    if (!ensure(4)) return 0;
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(data_[pos_ + i]) << (8 * i);
    pos_ += 4;
    return v;
  }
  std::uint64_t u64() {
    if (!ensure(8)) return 0;
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
    pos_ += 8;
    return v;
  }
  std::vector<std::uint8_t> take(std::size_t n) {
    if (!ensure(n)) return {};
    std::vector<std::uint8_t> v(data_.begin() + pos_, data_.begin() + pos_ + n);
    pos_ += n;
    return v;
  }

  std::size_t remaining() const { return error_ ? 0 : data_.size() - pos_; }
  bool error() const { return error_; }

 private:
  bool ensure(std::size_t n) {
    if (error_) return false;
    if (data_.size() - pos_ < n) {
      error_ = true;
      return false;
    }
    return true;
  }
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
  bool error_ = false;
};

// ---- per-message body encoders ----

void put_welcome_body(ByteWriter& w, const Welcome& m) {
  w.u16(m.protocol_version);
  w.u8(m.codec_id);
  w.u16(m.width);
  w.u16(m.height);
  w.u32(m.bitrate_kbps);
  w.u16(m.fps);
  w.u8(m.session_flags);
}

// ERROR/DISCONNECT share the {u16 code, u16 reason_len, reason bytes} body. Clamp the reason to the
// u16 length field so the emitted frame is always self-consistent (reason_len == bytes written);
// reasons are short diagnostic text, so the assert catches misuse without ever firing in practice.
void put_reason(ByteWriter& w, std::uint16_t code, const std::string& reason) {
  assert(reason.size() <= 0xFFFFu);
  const std::uint16_t n = static_cast<std::uint16_t>(std::min<std::size_t>(reason.size(), 0xFFFFu));
  w.u16(code);
  w.u16(n);
  w.bytes({reinterpret_cast<const std::uint8_t*>(reason.data()), n});
}

std::pair<Channel, std::uint8_t> body_of(const Message& msg, std::vector<std::uint8_t>& payload) {
  ByteWriter w(payload);
  Channel ch = Channel::Control;
  std::uint8_t type = 0;
  std::visit(
      [&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, Hello>) {
          ch = Channel::Control; type = ctrl::kHello;
          w.u16(m.protocol_version); w.u8(m.codec_mask); w.u16(m.max_width);
          w.u16(m.max_height); w.u32(m.max_bitrate_kbps); w.u16(m.max_fps); w.u8(m.feature_flags);
        } else if constexpr (std::is_same_v<T, Welcome>) {
          ch = Channel::Control; type = ctrl::kWelcome; put_welcome_body(w, m);
        } else if constexpr (std::is_same_v<T, FormatChange>) {
          ch = Channel::Control; type = ctrl::kFormatChange; put_welcome_body(w, m.format);
        } else if constexpr (std::is_same_v<T, ErrorMsg>) {
          ch = Channel::Control; type = ctrl::kError; put_reason(w, m.code, m.reason);
        } else if constexpr (std::is_same_v<T, Disconnect>) {
          ch = Channel::Control; type = ctrl::kDisconnect; put_reason(w, m.code, m.reason);
        } else if constexpr (std::is_same_v<T, KeyframeRequest>) {
          ch = Channel::Control; type = ctrl::kKeyframeRequest;
          w.u8(m.reason); w.u32(m.last_good_seq);
        } else if constexpr (std::is_same_v<T, Ping>) {
          ch = Channel::Control; type = ctrl::kPing; w.u64(m.token);
        } else if constexpr (std::is_same_v<T, Pong>) {
          ch = Channel::Control; type = ctrl::kPong; w.u64(m.token);
        } else if constexpr (std::is_same_v<T, VideoFrame>) {
          ch = Channel::Video; type = vid::kVideoFrame;
          w.u8(m.codec_id); w.u8(m.frame_type); w.u8(m.vflags); w.u64(m.pts_us); w.u32(m.seq);
          w.bytes(m.bitstream);
        }
      },
      msg);
  return {ch, type};
}

// ---- per-message body decoders. Each consumes `r` and must end fully drained. ----

bool decode_welcome_body(ByteReader& r, Welcome& out) {
  out.protocol_version = r.u16();
  out.codec_id = r.u8();
  out.width = r.u16();
  out.height = r.u16();
  out.bitrate_kbps = r.u32();
  out.fps = r.u16();
  out.session_flags = r.u8();
  return !r.error();
}

}  // namespace

std::vector<std::uint8_t> encode_frame(Channel channel, std::uint8_t type,
                                       std::span<const std::uint8_t> payload) {
  // Precondition (contract, not input validation): callers build these payloads, so a payload
  // larger than the protocol's own bound is a bug. Enforcing it here guarantees the encoder can
  // never emit a frame its own decoder would reject as OversizedMessage. length is computed in
  // 64-bit so the u32 narrowing is provably safe given the bound.
  assert(payload.size() <= kMaxPayload);
  const std::uint64_t length64 = std::uint64_t{3} + payload.size();
  const std::uint32_t length = static_cast<std::uint32_t>(length64);
  std::vector<std::uint8_t> out;
  out.reserve(std::size_t{4} + length);
  ByteWriter w(out);
  w.u32(length);
  w.u8(static_cast<std::uint8_t>(channel));
  w.u8(type);
  w.u8(0);  // flags reserved = 0
  w.bytes(payload);
  return out;
}

std::vector<std::uint8_t> encode(const Message& msg) {
  std::vector<std::uint8_t> payload;
  auto [ch, type] = body_of(msg, payload);
  return encode_frame(ch, type, payload);
}

std::optional<Message> decode_message(const RawFrame& frame, WireError& err) {
  err = WireError::Ok;
  if (frame.flags != 0) { err = WireError::ReservedFlagsSet; return std::nullopt; }

  ByteReader r(frame.payload);
  auto fail = [&](WireError e) -> std::optional<Message> { err = e; return std::nullopt; };
  auto finish = [&](Message m) -> std::optional<Message> {
    if (r.error()) return fail(WireError::ShortBuffer);
    if (r.remaining() != 0) return fail(WireError::TrailingBytes);
    return m;
  };

  if (frame.channel == Channel::Control) {
    switch (frame.type) {
      case ctrl::kHello: {
        Hello m;
        m.protocol_version = r.u16(); m.codec_mask = r.u8(); m.max_width = r.u16();
        m.max_height = r.u16(); m.max_bitrate_kbps = r.u32(); m.max_fps = r.u16();
        m.feature_flags = r.u8();
        if (r.error()) return fail(WireError::ShortBuffer);
        if (m.protocol_version != kProtocolVersion) return fail(WireError::UnsupportedVersion);
        return finish(m);
      }
      case ctrl::kWelcome: {
        Welcome m;
        if (!decode_welcome_body(r, m)) return fail(WireError::ShortBuffer);
        if (m.protocol_version != kProtocolVersion) return fail(WireError::UnsupportedVersion);
        return finish(m);
      }
      case ctrl::kFormatChange: {
        FormatChange m;
        if (!decode_welcome_body(r, m.format)) return fail(WireError::ShortBuffer);
        if (m.format.protocol_version != kProtocolVersion) return fail(WireError::UnsupportedVersion);
        return finish(m);
      }
      case ctrl::kError:
      case ctrl::kDisconnect: {
        const std::uint16_t code = r.u16();
        const std::uint16_t reason_len = r.u16();
        if (r.error()) return fail(WireError::ShortBuffer);
        if (r.remaining() != reason_len) return fail(WireError::BadField);
        auto bytes = r.take(reason_len);
        std::string reason(bytes.begin(), bytes.end());
        if (frame.type == ctrl::kError) return finish(ErrorMsg{code, std::move(reason)});
        return finish(Disconnect{code, std::move(reason)});
      }
      case ctrl::kKeyframeRequest: {
        KeyframeRequest m;
        m.reason = r.u8(); m.last_good_seq = r.u32();
        if (r.error()) return fail(WireError::ShortBuffer);
        return finish(m);
      }
      case ctrl::kPing: {
        Ping m; m.token = r.u64();
        if (r.error()) return fail(WireError::ShortBuffer);
        return finish(m);
      }
      case ctrl::kPong: {
        Pong m; m.token = r.u64();
        if (r.error()) return fail(WireError::ShortBuffer);
        return finish(m);
      }
      default:
        return fail(WireError::UnknownType);
    }
  }

  if (frame.channel == Channel::Video) {
    if (frame.type != vid::kVideoFrame) return fail(WireError::UnknownType);
    VideoFrame m;
    m.codec_id = r.u8(); m.frame_type = r.u8(); m.vflags = r.u8();
    m.pts_us = r.u64(); m.seq = r.u32();
    if (r.error()) return fail(WireError::ShortBuffer);
    m.bitstream = r.take(r.remaining());  // rest of payload, may be empty
    return m;  // no trailing-bytes check: bitstream absorbs the remainder
  }

  return fail(WireError::UnknownChannel);
}

void FrameParser::feed(std::span<const std::uint8_t> bytes) {
  // Compact occasionally so the buffer can't grow unbounded as frames are consumed.
  if (read_pos_ > 0 && read_pos_ == buf_.size()) {
    buf_.clear();
    read_pos_ = 0;
  } else if (read_pos_ > 64 * 1024) {
    buf_.erase(buf_.begin(), buf_.begin() + read_pos_);
    read_pos_ = 0;
  }
  buf_.insert(buf_.end(), bytes.begin(), bytes.end());
}

std::optional<RawFrame> FrameParser::next(WireError& err) {
  err = WireError::Ok;
  const std::size_t avail = buf_.size() - read_pos_;
  if (avail < 4) { err = WireError::NeedMoreData; return std::nullopt; }

  const std::uint8_t* p = buf_.data() + read_pos_;
  std::uint32_t length = 0;
  for (int i = 0; i < 4; ++i) length |= static_cast<std::uint32_t>(p[i]) << (8 * i);

  if (length < 3) { err = WireError::BadField; return std::nullopt; }
  if (length - 3 > kMaxPayload) { err = WireError::OversizedMessage; return std::nullopt; }
  if (avail < 4u + length) { err = WireError::NeedMoreData; return std::nullopt; }

  RawFrame frame;
  const std::uint8_t channel_byte = p[4];
  if (channel_byte != static_cast<std::uint8_t>(Channel::Control) &&
      channel_byte != static_cast<std::uint8_t>(Channel::Video)) {
    err = WireError::UnknownChannel;
    return std::nullopt;
  }
  frame.channel = static_cast<Channel>(channel_byte);
  frame.type = p[5];
  frame.flags = p[6];
  const std::size_t payload_len = length - 3;
  frame.payload.assign(p + 7, p + 7 + payload_len);

  read_pos_ += 4 + length;
  return frame;
}

}  // namespace td::proto
