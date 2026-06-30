// Parsec VDD-backed IVirtualDisplay (Windows only). Drives Parsec's proprietary, pre-signed IddCx
// driver via the verified IOCTL protocol in vdd_protocol.hpp (Option A1, R6-approved).
//
// ⛔ This file is NOT compiled or tested in the agent's WSL environment (it is pure Win32). It is
//    built by the project's CMake on Windows (MSVC) and verified on hardware via
//    sandboxes/vdd_sandbox.cpp. Hardware-dependent assumptions are tagged [VERIFY-ON-HW].
//
// Design notes:
//  - The driver only ADDs a display and hands back an index; it does NOT set the resolution. Windows
//    enumerates a new monitor whose mode list comes from the driver's EDID + the custom resolutions
//    registered under HKLM\SOFTWARE\Parsec\vdd. We pick the closest listed mode (PickBestMode) and
//    apply it with ChangeDisplaySettingsEx. To get exact 2360x1640 the operator must add that custom
//    resolution first (see docs/RUNBOOK.md, M1.1).
//  - We identify the freshly-added monitor by set-difference of attached display names before/after
//    ADD, so we never touch the user's existing screens and need not hardcode the adapter's name.
//  - The driver drops any display not refreshed within ~100 ms, so a background thread pings UPDATE.
#if defined(_WIN32)

#include "td/vdisplay/vdd_protocol.hpp"
#include "td/vdisplay/virtual_display.hpp"

#include <windows.h>
#include <setupapi.h>

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "user32.lib")

namespace td::vdisplay {
namespace {

GUID ToWinGuid(const parsec::Guid& g) {
  GUID out;
  out.Data1 = g.data1;
  out.Data2 = g.data2;
  out.Data3 = g.data3;
  for (int i = 0; i < 8; ++i) out.Data4[i] = g.data4[i];
  return out;
}

// Open a handle to the Parsec VDD device interface. Returns INVALID_HANDLE_VALUE on failure and sets
// `interface_present` to whether the device interface existed at all (true => the interface is there
// but the open failed; false => the driver is not installed). Lets the caller report OpenFailed vs
// DriverNotFound distinctly.
HANDLE OpenVddDevice(bool& interface_present) {
  interface_present = false;
  const GUID guid = ToWinGuid(parsec::kAdapterGuid);
  HDEVINFO info = SetupDiGetClassDevs(&guid, nullptr, nullptr,
                                      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (info == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

  HANDLE handle = INVALID_HANDLE_VALUE;
  SP_DEVICE_INTERFACE_DATA iface{};
  iface.cbSize = sizeof(iface);
  for (DWORD i = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &guid, i, &iface); ++i) {
    interface_present = true;  // interface exists; whether CreateFile succeeds is a separate question
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &needed, nullptr);
    if (needed == 0) continue;
    std::vector<std::uint8_t> buf(needed);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, needed, nullptr, nullptr)) {
      handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle != INVALID_HANDLE_VALUE) break;
    }
  }
  SetupDiDestroyDeviceInfoList(info);
  return handle;
}

// Synchronous IOCTL; returns the DWORD the driver writes back (display index / version), or -1.
int VddIoControl(HANDLE vdd, std::uint32_t code, const void* in, std::size_t in_size) {
  DWORD out = 0;
  DWORD returned = 0;
  BOOL ok = DeviceIoControl(vdd, code, const_cast<void*>(in), static_cast<DWORD>(in_size), &out,
                            sizeof(out), &returned, nullptr);
  return ok ? static_cast<int>(out) : -1;
}

int VddVersion(HANDLE vdd) { return VddIoControl(vdd, parsec::kIoctlVersion, nullptr, 0); }
void VddUpdate(HANDLE vdd) { VddIoControl(vdd, parsec::kIoctlUpdate, nullptr, 0); }

int VddAddDisplay(HANDLE vdd) {
  int index = VddIoControl(vdd, parsec::kIoctlAdd, nullptr, 0);
  VddUpdate(vdd);
  return index;
}

void VddRemoveDisplay(HANDLE vdd, int index) {
  const std::uint16_t data = parsec::EncodeRemoveIndex(index);
  VddIoControl(vdd, parsec::kIoctlRemove, &data, sizeof(data));
  VddUpdate(vdd);
}

std::set<std::wstring> AttachedDisplayNames() {
  std::set<std::wstring> names;
  DISPLAY_DEVICEW dev{};
  dev.cb = sizeof(dev);
  for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dev, 0); ++i) {
    if (dev.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) names.insert(dev.DeviceName);
    dev.cb = sizeof(dev);
  }
  return names;
}

std::vector<DisplayMode> EnumModes(const std::wstring& device) {
  std::vector<DisplayMode> modes;
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  for (DWORD i = 0; EnumDisplaySettingsW(device.c_str(), i, &dm); ++i) {
    modes.push_back(DisplayMode{static_cast<std::uint32_t>(dm.dmPelsWidth),
                                static_cast<std::uint32_t>(dm.dmPelsHeight),
                                static_cast<std::uint32_t>(dm.dmDisplayFrequency)});
    dm.dmSize = sizeof(dm);
  }
  return modes;
}

bool ApplyMode(const std::wstring& device, const DisplayMode& mode) {
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  dm.dmPelsWidth = mode.width;
  dm.dmPelsHeight = mode.height;
  dm.dmDisplayFrequency = mode.refresh_hz;
  dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
  // flags = 0: apply dynamically (effective immediately) WITHOUT persisting the mode per-device in the
  // registry — this virtual display is created and destroyed each session, so registry state would
  // only accumulate stale entries.
  return ChangeDisplaySettingsExW(device.c_str(), &dm, nullptr, 0, nullptr) == DISP_CHANGE_SUCCESSFUL;
}

class ParsecVirtualDisplay final : public IVirtualDisplay {
 public:
  ~ParsecVirtualDisplay() override { Remove(); }  // Remove() stops the keep-alive and closes the handle

  VddStatus Add(const DisplayMode& desired, VirtualDisplayInfo& out) override {
    Remove();  // re-entrant: tear down any display this object already owns and close any open handle

    bool interface_present = false;
    vdd_ = OpenVddDevice(interface_present);
    if (vdd_ == INVALID_HANDLE_VALUE)
      return interface_present ? VddStatus::OpenFailed : VddStatus::DriverNotFound;
    if (VddVersion(vdd_) < 0) return VddStatus::DriverNotRunning;

    const auto before = AttachedDisplayNames();
    index_ = VddAddDisplay(vdd_);
    if (index_ < 0) return VddStatus::AddFailed;

    // Keep-alive must run before Windows would otherwise reclaim the new display.
    StartKeepAlive();

    const std::wstring device = WaitForNewDisplay(before);
    if (device.empty()) {
      Remove();
      return VddStatus::AddFailed;  // [VERIFY-ON-HW] driver added an index but no monitor attached
    }

    const auto chosen = PickBestMode(desired, EnumModes(device));
    if (!chosen || !ApplyMode(device, *chosen)) {
      Remove();
      return VddStatus::ModeUnavailable;
    }

    alive_ = true;
    out.driver_index = index_;
    out.active_mode = *chosen;
    out.gdi_device_name = device;
    return VddStatus::Ok;
  }

  void Remove() override {
    StopKeepAlive();
    if (vdd_ != INVALID_HANDLE_VALUE && index_ >= 0) {
      VddRemoveDisplay(vdd_, index_);
      index_ = -1;
    }
    if (vdd_ != INVALID_HANDLE_VALUE) {
      CloseHandle(vdd_);
      vdd_ = INVALID_HANDLE_VALUE;
    }
    alive_ = false;
  }

  bool IsAlive() const override { return alive_; }

 private:
  void StartKeepAlive() {
    if (keep_alive_.joinable()) return;  // defensive: never assign over a joinable thread
    keep_alive_run_ = true;
    keep_alive_ = std::thread([this] {
      while (keep_alive_run_.load()) {
        VddUpdate(vdd_);
        std::this_thread::sleep_for(std::chrono::milliseconds(parsec::kKeepAliveIntervalMs));
      }
    });
  }

  void StopKeepAlive() {
    keep_alive_run_ = false;
    if (keep_alive_.joinable()) keep_alive_.join();
  }

  // The new monitor may take a moment to attach after ADD; poll the attached set for ~5 s.
  std::wstring WaitForNewDisplay(const std::set<std::wstring>& before) {
    for (int i = 0; i < 50; ++i) {
      for (const auto& name : AttachedDisplayNames()) {
        if (before.find(name) == before.end()) return name;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return {};
  }

  HANDLE vdd_ = INVALID_HANDLE_VALUE;
  int index_ = -1;
  std::atomic<bool> keep_alive_run_{false};
  std::thread keep_alive_;
  bool alive_ = false;
};

}  // namespace

std::unique_ptr<IVirtualDisplay> MakeParsecVirtualDisplay() {
  return std::make_unique<ParsecVirtualDisplay>();
}

}  // namespace td::vdisplay

#endif  // _WIN32
