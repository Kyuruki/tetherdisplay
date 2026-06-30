// StreamingSession (M4) — the live capture→encode→§5→channel loop, plus a control reader that handles
// the iPad's KEYFRAME_REQUEST / PING / DISCONNECT. Orchestrates only abstract interfaces + the portable
// protocol, so it compiles and is unit-testable off-device (the Win32 capture/encoder are injected).
#ifndef TD_CORE_STREAMING_SESSION_HPP
#define TD_CORE_STREAMING_SESSION_HPP

#include "td/capture/capture_source.hpp"
#include "td/core/byte_channel.hpp"
#include "td/encode/encoder.hpp"
#include "td/protocol/wire.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace td::core {

// Build a §5 VIDEO_FRAME from an encoded packet. Pure — unit-tested. `pts_100ns` is the capture
// timestamp in 100-ns ticks (converted to microseconds for the wire).
proto::VideoFrame BuildVideoFrame(encode::Codec codec, bool is_keyframe, std::uint64_t pts_100ns,
                                  std::uint32_t seq, const std::uint8_t* data, std::size_t size);

class StreamingSession {
 public:
  StreamingSession(capture::ICaptureSource& capture, encode::IEncoder& encoder, IByteChannel& channel,
                   encode::EncoderConfig config, capture::CaptureTarget target)
      : capture_(capture), encoder_(encoder), channel_(channel),
        config_(std::move(config)), target_(std::move(target)) {}

  // Sends the opening HELLO/WELCOME, starts capture + the control-reader thread, and blocks until the
  // peer disconnects, an error occurs, or Stop() is called. Returns false if it could not start.
  bool Run();
  void Stop();

  std::uint32_t frames_sent() const { return seq_.load(); }

 private:
  void OnFrame(const capture::CapturedFrame& f);
  void ControlLoop();
  void Send(const proto::Message& msg);

  capture::ICaptureSource& capture_;
  encode::IEncoder& encoder_;
  IByteChannel& channel_;
  encode::EncoderConfig config_;
  capture::CaptureTarget target_;

  std::mutex send_mutex_;                 // serialize writes (video frames + pongs)
  std::atomic<bool> running_{false};
  std::atomic<bool> force_idr_{true};     // the first frame is always a keyframe
  std::atomic<std::uint32_t> seq_{0};
  bool encoder_ready_ = false;            // touched only on the capture thread
  std::thread control_thread_;
};

}  // namespace td::core

#endif  // TD_CORE_STREAMING_SESSION_HPP
