// Explicit cross-adapter texture bridge (§6.4) — Windows only.
// FALLBACK path: the primary M1.2 strategy is to run the WGC capture device on the encoder (NVIDIA)
// adapter so DWM hands us an already-resident texture. This bridge exists for the case where that is
// not viable and we must move a texture from a producer device (e.g. iGPU) to a consumer device
// (e.g. RTX 4080) ourselves, via an NT-handle keyed-mutex shared texture.
//
// Verified recipe (MS Learn): source texture with
//   D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
// IDXGIResource1::CreateSharedHandle -> ID3D11Device1::OpenSharedResource1, serialized with
// IDXGIKeyedMutex Acquire/Release (key ping-pong 0<->1) and a producer-side Flush. There is NO
// zero-copy across separate VRAM (PCIe traversal each frame), and keyed-mutex across two different-IHV
// adapters is historically driver-finicky: [VERIFY-ON-HW]. See docs/ARCHITECTURE.md.
#ifndef TD_CAPTURE_CROSS_ADAPTER_HPP
#define TD_CAPTURE_CROSS_ADAPTER_HPP
#if defined(_WIN32)

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace td::capture {

// Bridges one fixed-size BGRA8 texture from a producer device to a consumer device. Create once for a
// given resolution; Produce() on the capture side, Consume() on the encoder side, per frame.
class CrossAdapterBridge {
 public:
  CrossAdapterBridge();
  ~CrossAdapterBridge();
  CrossAdapterBridge(const CrossAdapterBridge&) = delete;
  CrossAdapterBridge& operator=(const CrossAdapterBridge&) = delete;

  // Allocate the shared texture on `producer` and open it on `consumer`. Returns false on failure
  // (e.g. WARP/REF device, unsupported format, share denied).
  bool Init(ID3D11Device* producer, ID3D11Device* consumer, std::uint32_t width,
            std::uint32_t height);

  // Copy `src` (on the producer device) into the shared texture. Producer-side, per frame.
  bool Produce(ID3D11DeviceContext* producer_ctx, ID3D11Texture2D* src);

  // Copy the shared texture into `dst` (on the consumer device). Consumer-side, per frame.
  bool Consume(ID3D11DeviceContext* consumer_ctx, ID3D11Texture2D* dst);

  bool valid() const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace td::capture

#endif  // _WIN32
#endif  // TD_CAPTURE_CROSS_ADAPTER_HPP
