// Portable adapter-selection + cross-adapter policy for capture (M1.2). No Win32/D3D types here, so
// this is unit-tested anywhere. The Win32 capture code fills AdapterInfo from DXGI_ADAPTER_DESC1 and
// uses these helpers to decide whether a cross-adapter copy is required and which GPU runs NVENC.
#ifndef TD_CAPTURE_ADAPTER_HPP
#define TD_CAPTURE_ADAPTER_HPP

#include <cstdint>
#include <optional>
#include <span>

namespace td::capture {

// Portable mirror of a Windows LUID (a locally-unique, session-stable adapter id).
struct AdapterId {
  std::uint32_t low = 0;
  std::int32_t  high = 0;
  bool operator==(const AdapterId&) const = default;
};

struct AdapterInfo {
  AdapterId     id;
  std::uint32_t vendor_id = 0;        // PCI vendor id (from DXGI_ADAPTER_DESC1.VendorId)
  std::uint64_t dedicated_vram = 0;   // bytes (DedicatedVideoMemory)
  bool operator==(const AdapterInfo&) const = default;
};

inline constexpr std::uint32_t kVendorNvidia = 0x10DE;
inline constexpr std::uint32_t kVendorIntel  = 0x8086;
inline constexpr std::uint32_t kVendorAmd    = 0x1002;

// A captured texture on `capture` must be copied to `encoder` iff they are different adapters.
inline bool NeedsCrossAdapterCopy(const AdapterId& capture, const AdapterId& encoder) {
  return !(capture == encoder);
}

// Choose the adapter to run NVENC on: prefer NVIDIA (NVENC lives there); among candidates of the same
// preference rank, pick the most dedicated VRAM. Returns nullopt only for an empty list.
std::optional<AdapterInfo> SelectEncoderAdapter(std::span<const AdapterInfo> adapters);

}  // namespace td::capture

#endif  // TD_CAPTURE_ADAPTER_HPP
