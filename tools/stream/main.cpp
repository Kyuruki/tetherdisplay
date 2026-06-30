// M4 host live-streaming sandbox (R5) — the full continuous pipeline: virtual display -> WGC capture ->
// NVENC -> §5 VIDEO_FRAMEs -> usbmux, driven by core::StreamingSession, with the control reader handling
// the iPad's KEYFRAME_REQUEST / PING / DISCONNECT. Runs until the iPad disconnects or Ctrl+C.
//
//   td_stream [device_port]      (default 2345)
//
// ⛔ Windows-only (capture/encode/vdisplay + GPU); the agent cannot run it. With the iPad app (M3
//    M3ContentView) running, this shows the LIVE second screen — drag a window onto the virtual display.
#if defined(_WIN32)

#include "td/capture/capture_source.hpp"
#include "td/core/streaming_session.hpp"
#include "td/core/tunnel_channel.hpp"
#include "td/encode/encoder.hpp"
#include "td/usbmux/usbmux_client.hpp"
#include "td/vdisplay/virtual_display.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {
td::core::StreamingSession* g_session = nullptr;
BOOL WINAPI CtrlHandler(DWORD type) {
  if (type == CTRL_C_EVENT && g_session) { g_session->Stop(); return TRUE; }
  return FALSE;
}
}  // namespace

int main(int argc, char** argv) {
  const std::uint16_t device_port = (argc >= 2) ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 2345;

  auto display = td::vdisplay::MakeParsecVirtualDisplay();
  td::vdisplay::VirtualDisplayInfo vdi;
  if (auto st = display->Add({2360, 1640, 60}, vdi); st != td::vdisplay::VddStatus::Ok) {
    std::fprintf(stderr, "virtual display Add failed: %s\n", td::vdisplay::to_string(st));
    return 1;
  }
  std::printf("Virtual display up: %u×%u (%ls)\n", vdi.active_mode.width, vdi.active_mode.height,
              vdi.gdi_device_name.c_str());

  td::usbmux::UsbmuxClient mux;
  td::usbmux::AttachedDevice dev;
  if (auto st = mux.WaitForDevice(15000, dev); st != td::usbmux::MuxStatus::Ok) {
    std::fprintf(stderr, "WaitForDevice: %s\n", td::usbmux::to_string(st));
    return 1;
  }
  td::usbmux::Tunnel tunnel;
  if (auto st = mux.Connect(dev.device_id, device_port, tunnel); st != td::usbmux::MuxStatus::Ok) {
    std::fprintf(stderr, "Connect: %s (is the iPad app listening on %u?)\n", td::usbmux::to_string(st),
                 device_port);
    return 1;
  }
  std::printf("Tunnel open to device %d port %u. Streaming live — Ctrl+C to stop.\n", dev.device_id,
              device_port);

  auto capture = td::capture::MakeWgcCaptureSource();
  auto encoder = td::encode::MakeNvencEncoder();
  td::core::TunnelChannel channel(tunnel);

  td::encode::EncoderConfig cfg;
  cfg.codec = td::encode::Codec::Hevc;
  cfg.width = vdi.active_mode.width;    // advertise the real resolution in HELLO/WELCOME
  cfg.height = vdi.active_mode.height;
  cfg.fps = 60;
  cfg.target_bitrate_kbps = 50000;
  cfg.max_bitrate_kbps = 80000;  // clamped to the USB-2 ceiling by the encoder

  td::core::StreamingSession session(*capture, *encoder, channel, cfg,
                                     td::capture::CaptureTarget{vdi.gdi_device_name});
  g_session = &session;
  SetConsoleCtrlHandler(CtrlHandler, TRUE);

  session.Run();  // blocks until the iPad disconnects or Ctrl+C

  SetConsoleCtrlHandler(CtrlHandler, FALSE);  // deregister before the session goes out of scope
  g_session = nullptr;
  std::printf("Stopped after %u frames.\n", session.frames_sent());
  display->Remove();
  return 0;
}

#else
int main() { return 0; }  // Windows-only live-streaming sandbox
#endif
