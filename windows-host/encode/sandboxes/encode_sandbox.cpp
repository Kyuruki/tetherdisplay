// M1.3 / M1 integration sandbox (R5) — Windows console app the OPERATOR runs on the XPS.
// Wires capture (M1.2) -> NVENC (M1.3): captures the virtual display and records a low-latency HEVC
// elementary stream to disk. This is the M1 gate: play the file back and confirm it shows the
// virtual second display.
//
//   td_encode_sandbox "\\.\DISPLAY3" [out.h265] [seconds]
//
// The agent cannot run this (no GPU/NVENC). Encoding happens inside the capture callback, on the
// capture worker thread, so the shared D3D11 immediate context is used single-threaded.
#if defined(_WIN32)

#include "td/capture/capture_source.hpp"
#include "td/encode/encoder.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
#include <thread>

namespace {
// Convert a wide command-line arg to a UTF-8 std::string (don't truncate each wchar_t to char).
std::string ToUtf8(const wchar_t* w) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string s(static_cast<std::size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
}  // namespace

using namespace td::capture;
using namespace td::encode;

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::printf("usage: td_encode_sandbox \"\\\\.\\DISPLAY3\" [out.h265] [seconds]\n");
    return 64;
  }
  const std::wstring device_name = argv[1];
  const std::string out_path = (argc >= 3) ? ToUtf8(argv[2]) : "capture.h265";
  const int seconds = (argc >= 4) ? _wtoi(argv[3]) : 3;

  std::ofstream out(out_path, std::ios::binary);
  if (!out) { std::fprintf(stderr, "cannot open %s\n", out_path.c_str()); return 1; }

  auto capture = MakeWgcCaptureSource();
  auto encoder = MakeNvencEncoder();
  if (!capture || !encoder) { std::fprintf(stderr, "factory returned null\n"); return 2; }

  std::atomic<uint64_t> frames{0}, packets{0}, bytes{0}, keyframes{0};
  std::atomic<int> enc_init{-1};  // -1 not attempted, 0 ok, >0 failed (EncodeStatus)
  bool inited = false;            // touched only on the (serialized) capture worker thread

  auto write_packet = [&](const EncodedPacket& p) {
    out.write(reinterpret_cast<const char*>(p.data), static_cast<std::streamsize>(p.size));
    packets.fetch_add(1);
    bytes.fetch_add(p.size);
    if (p.is_keyframe) keyframes.fetch_add(1);
  };

  auto on_frame = [&](const CapturedFrame& f) {
    if (!inited) {
      EncoderConfig cfg;
      cfg.codec = Codec::Hevc;
      cfg.width = f.width;
      cfg.height = f.height;
      cfg.fps = 60;
      cfg.target_bitrate_kbps = 50000;
      cfg.max_bitrate_kbps = 80000;
      const EncodeStatus st = encoder->Initialize(capture->GetDevice(), cfg);
      enc_init.store(static_cast<int>(st));
      inited = true;
      if (st != EncodeStatus::Ok) return;  // give up encoding; main thread will report
    }
    if (enc_init.load() != static_cast<int>(EncodeStatus::Ok)) return;

    const bool force_idr = (f.frame_id == 0);  // keyframe at startup
    encoder->EncodeFrame(f.encoder_texture, f.timestamp_100ns, force_idr, write_packet);
    frames.fetch_add(1);
  };

  const CaptureStatus cs = capture->Start(CaptureTarget{device_name}, on_frame);
  if (cs != CaptureStatus::Ok) {
    std::fprintf(stderr, "capture Start failed: %s\n", to_string(cs));
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  capture->Stop();              // joins the worker; no more frames after this returns
  encoder->Flush(write_packet); // drain end-of-stream
  encoder->Shutdown();
  out.close();

  if (enc_init.load() > 0) {
    std::fprintf(stderr, "encoder Initialize failed: %s\n",
                 to_string(static_cast<EncodeStatus>(enc_init.load())));
    return 1;
  }
  std::printf("Encoded %llu frames -> %llu packets, %llu bytes (%llu keyframes) to %s\n",
              (unsigned long long)frames.load(), (unsigned long long)packets.load(),
              (unsigned long long)bytes.load(), (unsigned long long)keyframes.load(),
              out_path.c_str());
  std::printf(">>> Play it back:  ffplay %s   (or VLC). It must show the VIRTUAL display's contents.\n",
              out_path.c_str());
  return (frames.load() > 0 && bytes.load() > 0) ? 0 : 1;
}

#else
int main() { return 0; }  // sandbox is Windows-only
#endif
