// StreamingSession — portable orchestration of the live loop. See streaming_session.hpp.
#include "td/core/streaming_session.hpp"

#include "td/core/control_plane.hpp"

#include <chrono>
#include <vector>

namespace td::core {

proto::VideoFrame BuildVideoFrame(encode::Codec codec, bool is_keyframe, std::uint64_t pts_100ns,
                                  std::uint32_t seq, const std::uint8_t* data, std::size_t size) {
  proto::VideoFrame vf;
  vf.codec_id = (codec == encode::Codec::H264) ? proto::kCodecH264 : proto::kCodecHEVC;
  vf.frame_type = is_keyframe ? proto::kFrameIDR : proto::kFrameDelta;
  vf.vflags = 0;
  vf.pts_us = pts_100ns / 10;  // 100-ns ticks -> microseconds
  vf.seq = seq;
  vf.bitstream.assign(data, data + size);
  return vf;
}

void StreamingSession::Send(const proto::Message& msg) {
  auto bytes = proto::encode(msg);
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (!channel_.SendAll(bytes)) running_ = false;
}

void StreamingSession::OnFrame(const capture::CapturedFrame& f) {
  if (!running_) return;
  if (!encoder_ready_) {
    config_.width = f.width;
    config_.height = f.height;
    if (encoder_.Initialize(capture_.GetDevice(), config_) != encode::EncodeStatus::Ok) {
      running_ = false;
      return;
    }
    encoder_ready_ = true;
  }

  const bool idr = force_idr_.exchange(false);
  encoder_.EncodeFrame(f.encoder_texture, f.timestamp_100ns, idr,
                       [this, &f](const encode::EncodedPacket& p) {
                         auto vf = BuildVideoFrame(config_.codec, p.is_keyframe, f.timestamp_100ns,
                                                   seq_.fetch_add(1), p.data, p.size);
                         Send(vf);
                       });
}

void StreamingSession::ControlLoop() {
  proto::FrameParser parser;
  std::vector<std::uint8_t> buf(64 * 1024);
  while (running_) {
    const std::int64_t n = channel_.Recv(buf);
    if (n <= 0) { running_ = false; break; }
    parser.feed(std::span<const std::uint8_t>(buf.data(), static_cast<std::size_t>(n)));
    while (running_) {
      proto::WireError perr = proto::WireError::Ok;
      auto raw = parser.next(perr);
      if (!raw) {
        if (perr != proto::WireError::NeedMoreData) running_ = false;  // corrupt stream → terminal
        break;
      }
      proto::WireError derr = proto::WireError::Ok;
      auto msg = proto::decode_message(*raw, derr);
      if (!msg) continue;  // skip a single bad message; framing stayed in sync
      const ControlActions act = HandleControlMessage(*msg);
      if (act.force_idr) force_idr_ = true;
      if (act.send_pong) Send(proto::Pong{act.pong_token});
      if (act.disconnect) { running_ = false; break; }
      // act.format_change / act.have_client_caps are captured by the control plane but intentionally
      // not acted on in M4: mid-session renegotiation + capability-driven adaptation are M5.
    }
  }
}

bool StreamingSession::Run() {
  running_ = true;

  // Announce the host's capabilities + the negotiated format.
  Send(proto::Hello{proto::kProtocolVersion, proto::kMaskHEVC | proto::kMaskH264,
                    static_cast<std::uint16_t>(config_.width),
                    static_cast<std::uint16_t>(config_.height), config_.max_bitrate_kbps,
                    static_cast<std::uint16_t>(config_.fps), 0});
  Send(proto::Welcome{proto::kProtocolVersion,
                      config_.codec == encode::Codec::H264 ? proto::kCodecH264 : proto::kCodecHEVC,
                      static_cast<std::uint16_t>(config_.width),
                      static_cast<std::uint16_t>(config_.height), config_.target_bitrate_kbps,
                      static_cast<std::uint16_t>(config_.fps), 0});

  control_thread_ = std::thread([this] { ControlLoop(); });

  if (capture_.Start(target_, [this](const capture::CapturedFrame& f) { OnFrame(f); }) !=
      capture::CaptureStatus::Ok) {
    running_ = false;
  }

  while (running_) std::this_thread::sleep_for(std::chrono::milliseconds(20));

  capture_.Stop();
  encoder_.Shutdown();
  if (control_thread_.joinable()) control_thread_.join();
  return true;
}

void StreamingSession::Stop() {
  running_ = false;
  channel_.Close();  // unblock the control thread's blocking Recv so Run() can join it
}

}  // namespace td::core
