// Portable adapter selection + capture status strings. No Win32 — unit-tested in WSL.
#include "td/capture/adapter.hpp"
#include "td/capture/capture_source.hpp"

namespace td::capture {

const char* to_string(CaptureStatus s) {
  switch (s) {
    case CaptureStatus::Ok: return "Ok";
    case CaptureStatus::MonitorNotFound: return "MonitorNotFound";
    case CaptureStatus::DeviceCreateFailed: return "DeviceCreateFailed";
    case CaptureStatus::WgcUnavailable: return "WgcUnavailable";
    case CaptureStatus::CrossAdapterFailed: return "CrossAdapterFailed";
    case CaptureStatus::StartFailed: return "StartFailed";
  }
  return "Unknown";
}

namespace {

// Preference rank: NVIDIA first (NVENC lives there), then anything else.
int preference_rank(const AdapterInfo& a) { return a.vendor_id == kVendorNvidia ? 1 : 0; }

}  // namespace

std::optional<AdapterInfo> SelectEncoderAdapter(std::span<const AdapterInfo> adapters) {
  const AdapterInfo* best = nullptr;
  for (const auto& a : adapters) {
    if (!best) {
      best = &a;
      continue;
    }
    const int ra = preference_rank(a);
    const int rb = preference_rank(*best);
    if (ra != rb) {
      if (ra > rb) best = &a;            // higher preference wins
    } else if (a.dedicated_vram > best->dedicated_vram) {
      best = &a;                          // tie on preference -> more VRAM wins
    }
  }
  if (!best) return std::nullopt;
  return *best;
}

}  // namespace td::capture
