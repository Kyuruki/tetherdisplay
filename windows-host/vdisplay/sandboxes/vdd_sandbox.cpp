// M1.1 hardware sandbox (R5) — Windows console app the OPERATOR runs on the XPS.
// Brings up one Parsec virtual display at (or nearest to) 2360x1640@60, keeps it alive while you
// inspect Windows Display Settings, and removes it cleanly on Enter. This is the M1.1 human gate;
// the agent cannot run it.
//
//   Build (CMake, Windows):  cmake --build build --config Debug --target td_vdd_sandbox
//   Run (Administrator):     build\...\td_vdd_sandbox.exe
#if defined(_WIN32)

#include "td/vdisplay/virtual_display.hpp"

#include <cstdio>
#include <string>

int main() {
  using namespace td::vdisplay;

  auto display = MakeParsecVirtualDisplay();
  if (!display) {
    std::fprintf(stderr, "MakeParsecVirtualDisplay returned null (non-Windows build?)\n");
    return 2;
  }

  const DisplayMode desired{2360, 1640, 60};
  VirtualDisplayInfo info;
  std::printf("Adding virtual display, requesting %ux%u@%u ...\n", desired.width, desired.height,
              desired.refresh_hz);

  const VddStatus st = display->Add(desired, info);
  if (st != VddStatus::Ok) {
    std::fprintf(stderr, "Add failed: %s\n", to_string(st));
    if (st == VddStatus::DriverNotFound)
      std::fprintf(stderr, "  -> Install the Parsec VDD driver first (see docs/RUNBOOK.md, M1.1).\n");
    if (st == VddStatus::ModeUnavailable)
      std::fprintf(stderr, "  -> 2360x1640 may not be registered; add it as a custom resolution "
                           "(RUNBOOK M1.1), or accept the nearest mode.\n");
    return 1;
  }

  std::wprintf(L"OK: display index %d on device %s\n", info.driver_index, info.gdi_device_name.c_str());
  std::printf("Active mode applied: %ux%u@%u%s\n", info.active_mode.width, info.active_mode.height,
              info.active_mode.refresh_hz,
              (info.active_mode == desired) ? "  (exact)" : "  (nearest available — see note)");
  if (!(info.active_mode == desired)) {
    std::printf("NOTE: exact 2360x1640 was not available; add it under HKLM\\SOFTWARE\\Parsec\\vdd "
                "to get native resolution (RUNBOOK M1.1).\n");
  }

  std::printf("\n>>> Open Windows Settings > System > Display. You should see a SECOND display at the\n"
              ">>> mode above. Drag a window onto it to confirm. The keep-alive is running.\n\n"
              "Press Enter to remove the virtual display and exit...\n");
  std::getchar();

  display->Remove();
  std::printf("Removed. The second display should now be gone from Display Settings.\n");
  return 0;
}

#else
int main() { return 0; }  // sandbox is Windows-only
#endif
