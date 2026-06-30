// IEncoder — the video-encoder abstraction (M1.3). NVENC is the backend, but native types stay behind
// void* so the interface + the rate-control policy compile and are testable anywhere. The encoder
// takes a BGRA D3D11 texture on the encoder (NVIDIA) adapter and emits an HEVC/H.264 elementary stream
// as packets; the caller writes them to disk (M1.3) or frames them onto the §5 video channel (M4).
#ifndef TD_ENCODE_ENCODER_HPP
#define TD_ENCODE_ENCODER_HPP

#include <cstdint>
#include <functional>
#include <memory>

namespace td::encode {

enum class Codec : std::uint8_t { Hevc, H264 };
const char* to_string(Codec c);

// Practical USB 2.0 video budget. The raw line rate is 480 Mbps, but real throughput after usbmux
// framing/overhead is far lower; keep the compressed stream well under this hard cap (§6.5).
inline constexpr std::uint32_t kUsb2VideoCeilingKbps = 80000;  // ~80 Mbps hard cap

struct EncoderConfig {
  Codec         codec = Codec::Hevc;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t fps = 60;
  std::uint32_t target_bitrate_kbps = 50000;  // conservative start ~50 Mbps
  std::uint32_t max_bitrate_kbps = 80000;     // hard cap; clamped to the USB-2 ceiling too
  bool          low_latency = true;
};

// The low-latency rate-control plan derived from a config (the testable core of the NVENC setup).
struct RateControlPlan {
  std::uint32_t average_bitrate_kbps = 0;
  std::uint32_t max_bitrate_kbps = 0;
  std::uint32_t vbv_buffer_size_bits = 0;     // ~1 frame for low latency (small VBV = low delay)
  std::uint32_t vbv_initial_delay_bits = 0;   // == buffer size for low latency
  std::uint32_t frame_interval_p = 1;         // distance between P/I frames; 1 => B-frames DISABLED
  bool          infinite_gop = true;          // infinite GOP + on-demand IDR (forced via EncodeFrame)
  bool operator==(const RateControlPlan&) const = default;
};

// Clamp the requested bitrate to <= min(config.max_bitrate_kbps, ceiling) and size the VBV for the
// chosen latency. Pure — no NVENC/Win32 — and unit-tested.
RateControlPlan PlanRateControl(const EncoderConfig& cfg,
                                std::uint32_t ceiling_kbps = kUsb2VideoCeilingKbps);

enum class EncodeStatus {
  Ok,
  SdkNotFound,        // nvEncodeAPI64.dll / NvEncodeAPICreateInstance unavailable
  UnsupportedDevice,  // no NVENC-capable device / codec
  SessionInitFailed,
  RegisterFailed,     // could not register/map the D3D11 input texture
  EncodeFailed,
  NotInitialized,
};
const char* to_string(EncodeStatus s);

// One encoded access unit. `data` points into the encoder's locked bitstream and is ONLY valid for
// the duration of the callback — copy it out (to a file or the wire) before returning.
struct EncodedPacket {
  const std::uint8_t* data = nullptr;
  std::size_t         size = 0;
  bool                is_keyframe = false;  // IDR
  std::uint64_t       timestamp = 0;        // echoes the value passed to EncodeFrame
  std::uint64_t       frame_index = 0;      // monotonic, starts at 0
};

using PacketCallback = std::function<void(const EncodedPacket&)>;

class IEncoder {
 public:
  virtual ~IEncoder() = default;
  IEncoder(const IEncoder&) = delete;
  IEncoder& operator=(const IEncoder&) = delete;

  // `d3d11_device` is the ID3D11Device on the encoder (NVIDIA) adapter that produced the input
  // textures (so NVENC and the textures share a device). Idempotent re-Initialize first shuts down.
  virtual EncodeStatus Initialize(void* d3d11_device, const EncoderConfig& cfg) = 0;

  // Encode one BGRA D3D11 texture (NV_ENC_BUFFER_FORMAT_ARGB; NVENC does RGB->YUV internally).
  // `force_idr` requests a keyframe (startup / loss recovery). Emits 0+ packets via `on_packet`.
  virtual EncodeStatus EncodeFrame(void* d3d11_texture, std::uint64_t timestamp, bool force_idr,
                                   const PacketCallback& on_packet) = 0;

  // Drain any buffered output at end-of-stream.
  virtual EncodeStatus Flush(const PacketCallback& on_packet) = 0;

  virtual void Shutdown() = 0;

 protected:
  IEncoder() = default;
};

// NVENC-backed encoder. Returns nullptr on non-Windows builds.
std::unique_ptr<IEncoder> MakeNvencEncoder();

}  // namespace td::encode

#endif  // TD_ENCODE_ENCODER_HPP
