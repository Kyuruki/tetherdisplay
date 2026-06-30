// M5 host live-streaming sandbox (R5) — the full hardened pipeline: virtual display -> WGC capture ->
// NVENC -> §5 -> AEAD-encrypted usbmux tunnel, with TOFU pairing (unknown device refused) and a
// reconnect loop (kill/replug the cable -> clean auto-recovery). Ctrl+C stops.
//
//   td_stream [device_port]      (default 2345)
//
// ⛔ Windows-only (capture/encode/vdisplay + GPU + Credential Manager); the agent cannot run it.
#if defined(_WIN32)

#include "td/capture/capture_source.hpp"
#include "td/core/streaming_session.hpp"
#include "td/core/tunnel_channel.hpp"
#include "td/crypto/credential_store.hpp"
#include "td/crypto/identity_store.hpp"
#include "td/crypto/secure_byte_channel.hpp"
#include "td/encode/encoder.hpp"
#include "td/usbmux/usbmux_client.hpp"
#include "td/vdisplay/virtual_display.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};
td::core::StreamingSession* g_session = nullptr;
BOOL WINAPI CtrlHandler(DWORD type) {
  if (type == CTRL_C_EVENT) {
    g_stop = true;
    if (g_session) g_session->Stop();
    return TRUE;
  }
  return FALSE;
}
}  // namespace

int main(int argc, char** argv) {
  const std::uint16_t device_port = (argc >= 2) ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 2345;
  if (!td::crypto::InitCrypto()) { std::fprintf(stderr, "crypto init failed\n"); return 1; }

  auto display = td::vdisplay::MakeParsecVirtualDisplay();
  td::vdisplay::VirtualDisplayInfo vdi;
  if (auto st = display->Add({2360, 1640, 60}, vdi); st != td::vdisplay::VddStatus::Ok) {
    std::fprintf(stderr, "virtual display Add failed: %s\n", td::vdisplay::to_string(st));
    return 1;
  }

  td::crypto::CredentialManagerStore store;
  td::crypto::Identity me = td::crypto::LoadOrCreateIdentity(store);
  SetConsoleCtrlHandler(CtrlHandler, TRUE);
  std::printf("Host identity ready. Streaming %u×%u. Ctrl+C to stop.\n", vdi.active_mode.width,
              vdi.active_mode.height);

  td::encode::EncoderConfig cfg;
  cfg.codec = td::encode::Codec::Hevc;
  cfg.width = vdi.active_mode.width;
  cfg.height = vdi.active_mode.height;
  cfg.fps = 60;
  cfg.target_bitrate_kbps = 50000;
  cfg.max_bitrate_kbps = 80000;

  while (!g_stop.load()) {
    td::usbmux::UsbmuxClient mux;
    td::usbmux::AttachedDevice dev;
    if (mux.WaitForDevice(5000, dev) != td::usbmux::MuxStatus::Ok) continue;  // poll for the iPad

    td::usbmux::Tunnel tunnel;
    if (mux.Connect(dev.device_id, device_port, tunnel) != td::usbmux::MuxStatus::Ok) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;  // app not listening yet
    }

    td::core::TunnelChannel raw(tunnel);
    td::crypto::SecureByteChannel secure;
    if (auto hr = secure.Establish(raw, me, td::crypto::Pairing::Role::Server, store);
        hr != td::crypto::HandshakeResult::Ok) {
      std::fprintf(stderr, "pairing failed: %s%s\n", td::crypto::to_string(hr),
                   hr == td::crypto::HandshakeResult::PeerRejected ? " (unknown/unpaired device refused)"
                                                                   : "");
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    std::printf("Paired + encrypted with device %d. Streaming…\n", dev.device_id);

    auto capture = td::capture::MakeWgcCaptureSource();
    auto encoder = td::encode::MakeNvencEncoder();
    td::core::StreamingSession session(*capture, *encoder, secure, cfg,
                                       td::capture::CaptureTarget{vdi.gdi_device_name});
    g_session = &session;
    session.Run();  // blocks until the iPad disconnects or Ctrl+C
    g_session = nullptr;
    std::printf("Disconnected after %u frames; will reconnect…\n", session.frames_sent());
  }

  SetConsoleCtrlHandler(CtrlHandler, FALSE);
  display->Remove();
  std::printf("Stopped.\n");
  return 0;
}

#else
int main() { return 0; }  // Windows-only live-streaming sandbox
#endif
