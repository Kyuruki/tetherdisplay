// Portable rate-control planning + status strings for the encoder (M1.3). No NVENC/Win32 — tested in WSL.
#include "td/encode/encoder.hpp"

#include <algorithm>

namespace td::encode {

const char* to_string(Codec c) {
  switch (c) {
    case Codec::Hevc: return "HEVC";
    case Codec::H264: return "H264";
  }
  return "Unknown";
}

const char* to_string(EncodeStatus s) {
  switch (s) {
    case EncodeStatus::Ok: return "Ok";
    case EncodeStatus::SdkNotFound: return "SdkNotFound";
    case EncodeStatus::UnsupportedDevice: return "UnsupportedDevice";
    case EncodeStatus::SessionInitFailed: return "SessionInitFailed";
    case EncodeStatus::RegisterFailed: return "RegisterFailed";
    case EncodeStatus::EncodeFailed: return "EncodeFailed";
    case EncodeStatus::NotInitialized: return "NotInitialized";
  }
  return "Unknown";
}

RateControlPlan PlanRateControl(const EncoderConfig& cfg, std::uint32_t ceiling_kbps) {
  RateControlPlan plan;

  // Never let the stream exceed the USB-2 budget or the configured hard cap.
  const std::uint32_t hard_cap = std::min(cfg.max_bitrate_kbps, ceiling_kbps);
  plan.average_bitrate_kbps = std::min(cfg.target_bitrate_kbps, hard_cap);
  plan.max_bitrate_kbps = hard_cap;

  // VBV (HRD) buffer: small = low end-to-end delay. One frame's worth of bits for low latency, one
  // second for a relaxed (non-low-latency) profile. fps is clamped to >=1 to avoid divide-by-zero.
  const std::uint32_t fps = std::max<std::uint32_t>(cfg.fps, 1);
  const std::uint64_t avg_bits_per_sec = static_cast<std::uint64_t>(plan.average_bitrate_kbps) * 1000;
  const std::uint32_t one_frame = static_cast<std::uint32_t>(avg_bits_per_sec / fps);
  plan.vbv_buffer_size_bits = cfg.low_latency ? one_frame
                                              : static_cast<std::uint32_t>(avg_bits_per_sec);
  plan.vbv_initial_delay_bits = plan.vbv_buffer_size_bits;

  plan.frame_interval_p = 1;     // B-frames disabled (they add reorder latency)
  plan.infinite_gop = true;      // infinite GOP; keyframes are forced on demand
  return plan;
}

}  // namespace td::encode
