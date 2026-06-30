// M3 host integration sandbox (R5) — ties the WHOLE host pipeline together and pushes ONE keyframe to
// the iPad: virtual display (M1.1) -> WGC capture (M1.2) -> NVENC encode (M1.3) -> §5 VIDEO_FRAME (M0)
// -> usbmux tunnel (M2). With the iPad gate app (M3ContentView) running, that frame appears on screen.
//
//   td_stream_keyframe [device_port]      (default 2345)
//
// ⛔ Windows-only (capture/encode/vdisplay are Win32 + GPU); the agent cannot run it.
#if defined(_WIN32)

#include "td/capture/capture_source.hpp"
#include "td/encode/encoder.hpp"
#include "td/protocol/wire.hpp"
#include "td/usbmux/usbmux_client.hpp"
#include "td/vdisplay/virtual_display.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char** argv) {
  const std::uint16_t device_port = (argc >= 2) ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 2345;

  // 1. Bring up the virtual display.
  auto display = td::vdisplay::MakeParsecVirtualDisplay();
  td::vdisplay::VirtualDisplayInfo vdi;
  if (auto st = display->Add({2360, 1640, 60}, vdi); st != td::vdisplay::VddStatus::Ok) {
    std::fprintf(stderr, "virtual display Add failed: %s\n", td::vdisplay::to_string(st));
    return 1;
  }
  std::printf("Virtual display up: %u×%u\n", vdi.active_mode.width, vdi.active_mode.height);

  // 2. Open a usbmux tunnel to the iPad gate app's port.
  td::usbmux::UsbmuxClient mux;
  td::usbmux::AttachedDevice dev;
  if (auto st = mux.WaitForDevice(10000, dev); st != td::usbmux::MuxStatus::Ok) {
    std::fprintf(stderr, "WaitForDevice: %s\n", td::usbmux::to_string(st));
    return 1;
  }
  td::usbmux::Tunnel tunnel;
  if (auto st = mux.Connect(dev.device_id, device_port, tunnel); st != td::usbmux::MuxStatus::Ok) {
    std::fprintf(stderr, "Connect: %s (is the iPad gate app listening on %u?)\n",
                 td::usbmux::to_string(st), device_port);
    return 1;
  }
  std::printf("Tunnel open to device %d port %u\n", dev.device_id, device_port);

  // 3. Announce the session over §5 (the iPad also sends HELLO + KEYFRAME_REQUEST).
  auto send_msg = [&](const td::proto::Message& m) {
    auto bytes = td::proto::encode(m);
    tunnel.SendAll(bytes);
  };
  const auto w16 = static_cast<std::uint16_t>(vdi.active_mode.width);
  const auto h16 = static_cast<std::uint16_t>(vdi.active_mode.height);
  send_msg(td::proto::Hello{1, td::proto::kMaskHEVC | td::proto::kMaskH264, w16, h16, 80000, 60, 0});
  send_msg(td::proto::Welcome{1, td::proto::kCodecHEVC, w16, h16, 60000, 60, 0});

  // 4. Capture one frame, NVENC-encode it as an IDR, frame it as VIDEO_FRAME, send it.
  auto capture = td::capture::MakeWgcCaptureSource();
  auto encoder = td::encode::MakeNvencEncoder();
  std::atomic<bool> sent{false};
  bool enc_inited = false;

  auto on_frame = [&](const td::capture::CapturedFrame& f) {
    if (sent.load()) return;
    if (!enc_inited) {
      td::encode::EncoderConfig cfg;
      cfg.codec = td::encode::Codec::Hevc;
      cfg.width = f.width;
      cfg.height = f.height;
      cfg.fps = 60;
      if (encoder->Initialize(capture->GetDevice(), cfg) != td::encode::EncodeStatus::Ok) return;
      enc_inited = true;
    }
    encoder->EncodeFrame(f.encoder_texture, f.timestamp_100ns, /*force_idr=*/true,
                         [&](const td::encode::EncodedPacket& p) {
                           td::proto::VideoFrame vf;
                           vf.codec_id = td::proto::kCodecHEVC;
                           vf.frame_type = td::proto::kFrameIDR;
                           vf.pts_us = f.timestamp_100ns / 10;  // 100ns ticks -> microseconds
                           vf.seq = 0;
                           vf.bitstream.assign(p.data, p.data + p.size);
                           send_msg(vf);
                           std::printf("Sent IDR VIDEO_FRAME: %zu bitstream bytes\n", p.size);
                           sent.store(true);
                         });
  };

  if (auto st = capture->Start(td::capture::CaptureTarget{vdi.gdi_device_name}, on_frame);
      st != td::capture::CaptureStatus::Ok) {
    std::fprintf(stderr, "capture Start: %s\n", td::capture::to_string(st));
    return 1;
  }

  for (int i = 0; i < 100 && !sent.load(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

  capture->Stop();
  encoder->Shutdown();
  display->Remove();

  if (!sent.load()) { std::fprintf(stderr, "no frame was sent\n"); return 1; }
  std::printf(">>> The iPad gate app should now display this captured keyframe.\n");
  return 0;
}

#else
int main() { return 0; }  // Windows-only integration sandbox
#endif
