// ICaptureSource — the capture abstraction (M1.2). Capture is inherently a Windows/D3D concern, but
// this interface keeps native types behind `void*` so the header compiles (and the contract is
// testable/mokable) anywhere. The WGC implementation (wgc_capture.cpp) delivers, per frame, a D3D11
// texture that already lives on the ENCODER adapter (after the cross-adapter bridge when needed), so
// M1.3 can hand it straight to NVENC.
#ifndef TD_CAPTURE_CAPTURE_SOURCE_HPP
#define TD_CAPTURE_CAPTURE_SOURCE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace td::capture {

enum class CaptureFormat : std::uint8_t {
  Bgra8,  // DXGI_FORMAT_B8G8R8A8_UNORM — WGC's native output
  Nv12,   // converted form some encoders prefer (decided in M1.3)
};

enum class CaptureStatus {
  Ok,
  MonitorNotFound,    // the requested display identity did not resolve to an HMONITOR
  DeviceCreateFailed, // D3D11 device creation on the capture/encoder adapter failed
  WgcUnavailable,     // Windows.Graphics.Capture not supported on this OS/SKU
  CrossAdapterFailed, // the shared-texture bridge could not be established
  StartFailed,
};
const char* to_string(CaptureStatus s);

// One captured frame. `encoder_texture` is a native ID3D11Texture2D* that lives on the encoder adapter
// (ready for NVENC). It is owned by the capture source and only valid for the duration of the callback.
struct CapturedFrame {
  void*         encoder_texture = nullptr;  // ID3D11Texture2D* on the encoder adapter
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  CaptureFormat format = CaptureFormat::Bgra8;
  std::uint64_t frame_id = 0;               // monotonic, starts at 0
  std::uint64_t timestamp_100ns = 0;        // WGC SystemRelativeTime, in 100-ns ticks (NOT raw QPC)
  bool          crossed_adapters = false;   // true if the virtual display is on a different GPU than NVENC
};

using FrameCallback = std::function<void(const CapturedFrame&)>;

// Identifies which display to capture. M1.1 hands us the GDI device name (e.g. L"\\\\.\\DISPLAY3"),
// which the implementation resolves to an HMONITOR; we never capture the primary by accident.
struct CaptureTarget {
  std::wstring gdi_device_name;
};

class ICaptureSource {
 public:
  virtual ~ICaptureSource() = default;
  ICaptureSource(const ICaptureSource&) = delete;
  ICaptureSource& operator=(const ICaptureSource&) = delete;

  // Begin capturing `target`, invoking `on_frame` for each frame on a capture thread. Frames arrive
  // as textures already resident on the encoder adapter. Idempotent re-Start first Stops.
  virtual CaptureStatus Start(const CaptureTarget& target, FrameCallback on_frame) = 0;
  virtual void Stop() = 0;
  virtual bool IsCapturing() const = 0;

  // The ID3D11Device (on the encoder adapter) that owns the delivered textures. Valid after a
  // successful Start(); nullptr otherwise. The encoder must use this SAME device so it can copy the
  // captured texture (textures cannot cross devices). Returned as void* to keep this header portable.
  virtual void* GetDevice() const = 0;

 protected:
  ICaptureSource() = default;
};

// WGC-backed capture that targets the encoder adapter (NVIDIA) and bridges across adapters when the
// virtual display is composited on a different GPU. Returns nullptr on non-Windows builds.
std::unique_ptr<ICaptureSource> MakeWgcCaptureSource();

}  // namespace td::capture

#endif  // TD_CAPTURE_CAPTURE_SOURCE_HPP
