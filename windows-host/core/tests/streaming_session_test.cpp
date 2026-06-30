// Live-loop test for StreamingSession (M4), driven entirely by fakes in WSL: a fake capture source
// delivering frames on its own thread, a fake encoder, and an in-memory byte channel. Verifies the
// host streams continuous VIDEO_FRAMEs (IDR first, deltas after), answers PING with PONG, and forces
// an IDR on KEYFRAME_REQUEST.
#include "td/core/streaming_session.hpp"

#include "td/protocol/wire.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace td;

namespace {

class FakeChannel : public core::IByteChannel {
 public:
  bool SendAll(std::span<const std::uint8_t> d) override {
    std::lock_guard<std::mutex> l(m_);
    if (!open_) return false;
    sent_.insert(sent_.end(), d.begin(), d.end());
    cv_.notify_all();
    return true;
  }
  std::int64_t Recv(std::span<std::uint8_t> buf) override {
    std::unique_lock<std::mutex> l(m_);
    cv_.wait(l, [&] { return !inbound_.empty() || !open_; });
    if (inbound_.empty()) return 0;  // closed
    const std::size_t n = std::min(buf.size(), inbound_.size());
    std::copy(inbound_.begin(), inbound_.begin() + n, buf.data());
    inbound_.erase(inbound_.begin(), inbound_.begin() + n);
    return static_cast<std::int64_t>(n);
  }
  void PushInbound(const std::vector<std::uint8_t>& d) {
    std::lock_guard<std::mutex> l(m_);
    inbound_.insert(inbound_.end(), d.begin(), d.end());
    cv_.notify_all();
  }
  void Close() override {
    std::lock_guard<std::mutex> l(m_);
    open_ = false;
    cv_.notify_all();
  }
  std::vector<std::uint8_t> Sent() {
    std::lock_guard<std::mutex> l(m_);
    return sent_;
  }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::vector<std::uint8_t> sent_, inbound_;
  bool open_ = true;
};

class FakeCapture : public capture::ICaptureSource {
 public:
  capture::CaptureStatus Start(const capture::CaptureTarget&, capture::FrameCallback cb) override {
    cb_ = std::move(cb);
    running_ = true;
    th_ = std::thread([this] {
      std::uint64_t id = 0;
      while (running_) {
        capture::CapturedFrame f;
        f.encoder_texture = reinterpret_cast<void*>(0x1);
        f.width = 1280;
        f.height = 720;
        f.frame_id = id;
        f.timestamp_100ns = id * 100000;
        if (cb_) cb_(f);
        ++id;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }
    });
    return capture::CaptureStatus::Ok;
  }
  void Stop() override {
    running_ = false;
    if (th_.joinable()) th_.join();
  }
  bool IsCapturing() const override { return running_; }
  void* GetDevice() const override { return reinterpret_cast<void*>(0x1); }

 private:
  capture::FrameCallback cb_;
  std::atomic<bool> running_{false};
  std::thread th_;
};

class FakeEncoder : public encode::IEncoder {
 public:
  encode::EncodeStatus Initialize(void*, const encode::EncoderConfig&) override {
    return encode::EncodeStatus::Ok;
  }
  encode::EncodeStatus EncodeFrame(void*, std::uint64_t, bool force_idr,
                                   const encode::PacketCallback& cb) override {
    const std::uint8_t fake[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    encode::EncodedPacket p;
    p.data = fake;
    p.size = sizeof(fake);
    p.is_keyframe = force_idr;  // the fake honors the requested keyframe flag
    if (cb) cb(p);
    return encode::EncodeStatus::Ok;
  }
  encode::EncodeStatus Flush(const encode::PacketCallback&) override { return encode::EncodeStatus::Ok; }
  void Shutdown() override {}
};

// Parse every complete §5 message out of a byte buffer.
std::vector<proto::Message> ParseAll(const std::vector<std::uint8_t>& bytes) {
  proto::FrameParser parser;
  parser.feed(bytes);
  std::vector<proto::Message> out;
  while (true) {
    proto::WireError e = proto::WireError::Ok;
    auto raw = parser.next(e);
    if (!raw) break;
    proto::WireError de = proto::WireError::Ok;
    if (auto m = proto::decode_message(*raw, de)) out.push_back(*m);
  }
  return out;
}

template <typename Pred>
bool WaitFor(FakeChannel& ch, Pred pred, int timeout_ms = 3000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred(ParseAll(ch.Sent()))) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

int countVideoFrames(const std::vector<proto::Message>& msgs) {
  int n = 0;
  for (auto& m : msgs) if (std::holds_alternative<proto::VideoFrame>(m)) ++n;
  return n;
}

}  // namespace

TEST(BuildVideoFrame, MapsFieldsAndConvertsPts) {
  const std::uint8_t bits[3] = {1, 2, 3};
  auto vf = core::BuildVideoFrame(encode::Codec::Hevc, /*is_keyframe=*/true, /*pts_100ns=*/1'000'000,
                                  /*seq=*/5, bits, sizeof(bits));
  EXPECT_EQ(vf.codec_id, proto::kCodecHEVC);
  EXPECT_EQ(vf.frame_type, proto::kFrameIDR);
  EXPECT_EQ(vf.pts_us, 100000u);  // 1,000,000 ticks of 100ns = 100,000 us
  EXPECT_EQ(vf.seq, 5u);
  EXPECT_EQ(vf.bitstream, std::vector<std::uint8_t>({1, 2, 3}));
  auto delta = core::BuildVideoFrame(encode::Codec::H264, false, 0, 0, bits, 3);
  EXPECT_EQ(delta.codec_id, proto::kCodecH264);
  EXPECT_EQ(delta.frame_type, proto::kFrameDelta);
}

TEST(StreamingSession, LiveLoopStreamsAndHandlesControl) {
  FakeChannel ch;
  FakeCapture cap;
  FakeEncoder enc;
  encode::EncoderConfig cfg;
  cfg.codec = encode::Codec::Hevc;
  cfg.width = 1280;
  cfg.height = 720;
  cfg.fps = 60;
  cfg.target_bitrate_kbps = 50000;
  cfg.max_bitrate_kbps = 80000;

  core::StreamingSession session(cap, enc, ch, cfg, capture::CaptureTarget{L"FAKE"});
  std::thread runner([&] { session.Run(); });

  // Continuous streaming: opening HELLO+WELCOME then several VIDEO_FRAMEs.
  ASSERT_TRUE(WaitFor(ch, [](const auto& msgs) { return countVideoFrames(msgs) >= 5; }));
  {
    auto msgs = ParseAll(ch.Sent());
    ASSERT_GE(static_cast<int>(msgs.size()), 2);
    ASSERT_TRUE(std::holds_alternative<proto::Hello>(msgs[0]));
    ASSERT_TRUE(std::holds_alternative<proto::Welcome>(msgs[1]));
    // The handshake must advertise the real resolution, not 0x0.
    EXPECT_EQ(std::get<proto::Hello>(msgs[0]).max_width, 1280u);
    EXPECT_EQ(std::get<proto::Hello>(msgs[0]).max_height, 720u);
    EXPECT_EQ(std::get<proto::Welcome>(msgs[1]).width, 1280u);
    EXPECT_EQ(std::get<proto::Welcome>(msgs[1]).height, 720u);
    // First video frame is an IDR; later ones are deltas (no keyframe requested yet).
    bool firstVideoIsIdr = false, sawDelta = false;
    for (auto& m : msgs) {
      if (auto* vf = std::get_if<proto::VideoFrame>(&m)) {
        if (!firstVideoIsIdr && vf->seq == 0) firstVideoIsIdr = (vf->frame_type == proto::kFrameIDR);
        if (vf->frame_type == proto::kFrameDelta) sawDelta = true;
      }
    }
    EXPECT_TRUE(firstVideoIsIdr);
    EXPECT_TRUE(sawDelta);
  }

  // PING -> PONG echoing the token.
  ch.PushInbound(proto::encode(proto::Ping{0x1234}));
  ASSERT_TRUE(WaitFor(ch, [](const auto& msgs) {
    for (auto& m : msgs)
      if (auto* p = std::get_if<proto::Pong>(&m)) if (p->token == 0x1234) return true;
    return false;
  }));

  // KEYFRAME_REQUEST -> a later VIDEO_FRAME is an IDR. Record the current high-water seq first.
  std::uint32_t seqBefore = 0;
  for (auto& m : ParseAll(ch.Sent()))
    if (auto* vf = std::get_if<proto::VideoFrame>(&m)) seqBefore = std::max(seqBefore, vf->seq);
  ch.PushInbound(proto::encode(proto::KeyframeRequest{1, 0}));
  ASSERT_TRUE(WaitFor(ch, [&](const auto& msgs) {
    for (auto& m : msgs)
      if (auto* vf = std::get_if<proto::VideoFrame>(&m))
        if (vf->seq > seqBefore && vf->frame_type == proto::kFrameIDR) return true;
    return false;
  }));

  // Stop() alone must unblock the control thread's Recv and let Run() join (no manual channel close):
  // this exercises the production Ctrl+C/disconnect shutdown path.
  session.Stop();
  runner.join();
}
