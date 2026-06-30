// Portable mode-selection + status strings for the vdisplay module. No Win32 — unit-tested in WSL.
#include "td/vdisplay/virtual_display.hpp"

#include <cstdint>
#include <cstdlib>

namespace td::vdisplay {

const char* to_string(VddStatus s) {
  switch (s) {
    case VddStatus::Ok: return "Ok";
    case VddStatus::DriverNotFound: return "DriverNotFound";
    case VddStatus::DriverNotRunning: return "DriverNotRunning";
    case VddStatus::OpenFailed: return "OpenFailed";
    case VddStatus::AddFailed: return "AddFailed";
    case VddStatus::ModeUnavailable: return "ModeUnavailable";
    case VddStatus::RemoveFailed: return "RemoveFailed";
  }
  return "Unknown";
}

namespace {

std::int64_t area(const DisplayMode& m) {
  return static_cast<std::int64_t>(m.width) * static_cast<std::int64_t>(m.height);
}

// Cross-multiply to compare aspect ratios without floating point.
bool same_aspect(const DisplayMode& a, const DisplayMode& b) {
  return static_cast<std::int64_t>(a.width) * b.height ==
         static_cast<std::int64_t>(b.width) * a.height;
}

std::int64_t refresh_diff(const DisplayMode& a, const DisplayMode& b) {
  return std::llabs(static_cast<std::int64_t>(a.refresh_hz) -
                    static_cast<std::int64_t>(b.refresh_hz));
}

// Is `a` a better tier-3 fit for `desired` than `b`?
bool better_fit(const DisplayMode& a, const DisplayMode& b, const DisplayMode& desired) {
  const bool aa = same_aspect(a, desired);
  const bool ba = same_aspect(b, desired);
  if (aa != ba) return aa;  // prefer equal aspect ratio

  const std::int64_t ad = std::llabs(area(a) - area(desired));
  const std::int64_t bd = std::llabs(area(b) - area(desired));
  if (ad != bd) return ad < bd;  // then closest pixel area

  return a.refresh_hz > b.refresh_hz;  // then higher refresh
}

}  // namespace

std::optional<DisplayMode> PickBestMode(const DisplayMode& desired,
                                        std::span<const DisplayMode> supported) {
  if (supported.empty()) return std::nullopt;

  // Tier 1: exact match.
  for (const auto& m : supported) {
    if (m == desired) return m;
  }

  // Tier 2: exact resolution, nearest refresh (higher wins ties).
  const DisplayMode* best = nullptr;
  for (const auto& m : supported) {
    if (m.width != desired.width || m.height != desired.height) continue;
    if (!best || refresh_diff(m, desired) < refresh_diff(*best, desired) ||
        (refresh_diff(m, desired) == refresh_diff(*best, desired) &&
         m.refresh_hz > best->refresh_hz)) {
      best = &m;
    }
  }
  if (best) return *best;

  // Tier 3: aspect-then-area-then-refresh.
  best = &supported.front();
  for (const auto& m : supported.subspan(1)) {
    if (better_fit(m, *best, desired)) best = &m;
  }
  return *best;
}

}  // namespace td::vdisplay
