// IVirtualDisplay: the swappable abstraction over the virtual-display driver (M1.1).
// The concrete backend for M1 is Parsec VDD (Option A1), but nothing above this interface knows
// that, so Option B (self-signed MS IddCx sample) can replace it later without touching capture.
// The mode-selection logic (PickBestMode) and status type live here and are pure/testable; the
// driver-touching implementation is Windows-only (src/parsec_vdd.cpp).
#ifndef TD_VDISPLAY_VIRTUAL_DISPLAY_HPP
#define TD_VDISPLAY_VIRTUAL_DISPLAY_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace td::vdisplay {

struct DisplayMode {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t refresh_hz = 0;
  bool operator==(const DisplayMode&) const = default;
};

// Choose the supported mode that best matches `desired`:
//   1. exact (width, height, refresh) if present;
//   2. else exact (width, height) with the nearest refresh (higher wins ties);
//   3. else the mode preferring equal aspect ratio, then smallest pixel-area difference, then
//      higher refresh.
// Returns nullopt only when `supported` is empty. Pure — no Win32 — and unit-tested.
std::optional<DisplayMode> PickBestMode(const DisplayMode& desired,
                                        std::span<const DisplayMode> supported);

enum class VddStatus {
  Ok,
  DriverNotFound,    // the Parsec VDD device interface is not present (driver not installed)
  DriverNotRunning,  // device present but the version handshake failed
  OpenFailed,        // CreateFile on the device handle failed
  AddFailed,         // the ADD ioctl did not return a valid index
  ModeUnavailable,   // the display came up but the requested mode could not be applied
  RemoveFailed,
};
const char* to_string(VddStatus s);

struct VirtualDisplayInfo {
  int          driver_index = -1;     // index the driver's ADD ioctl returned
  DisplayMode  active_mode;           // the mode actually applied (may differ from requested)
  std::wstring gdi_device_name;       // e.g. L"\\\\.\\DISPLAY3" — lets capture target THIS display,
                                      // never the user's primary screen
};

// Owns exactly one virtual display. Add() brings it up and starts the keep-alive; Remove() (and the
// destructor) tears it down and stops the keep-alive. Move-only conceptually; non-copyable.
class IVirtualDisplay {
 public:
  virtual ~IVirtualDisplay() = default;
  IVirtualDisplay(const IVirtualDisplay&) = delete;
  IVirtualDisplay& operator=(const IVirtualDisplay&) = delete;

  // Brings up one virtual display matching `desired` as closely as the driver allows (see
  // PickBestMode), starts its keep-alive, and fills `out`. Calling Add() again first removes any
  // display this object currently owns (re-entrant). Returns VddStatus::Ok on success.
  virtual VddStatus Add(const DisplayMode& desired, VirtualDisplayInfo& out) = 0;
  // Tears down the owned display (if any) and stops the keep-alive. Idempotent — safe to call twice.
  virtual void Remove() = 0;
  virtual bool IsAlive() const = 0;

 protected:
  IVirtualDisplay() = default;
};

// Parsec VDD-backed implementation. Returns nullptr on non-Windows builds.
std::unique_ptr<IVirtualDisplay> MakeParsecVirtualDisplay();

}  // namespace td::vdisplay

#endif  // TD_VDISPLAY_VIRTUAL_DISPLAY_HPP
