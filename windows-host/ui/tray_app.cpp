// Minimal host tray UI (M6, §6.7 control surface). A system-tray icon with Start / Stop / Quit that
// drives the same connect -> pair -> stream -> reconnect loop as tools/stream, on a worker thread.
// Status is shown in the tray tooltip. Config is loaded from %APPDATA%\TetherDisplay\config.txt.
//
// ⛔ Windows-only (Win32 GUI + GPU + Credential Manager); the agent cannot compile/run it. The
//    streaming core it drives IS unit-tested in WSL (core/crypto tests); this file is the thin shell.
#if defined(_WIN32)

#include "td/capture/capture_source.hpp"
#include "td/core/config.hpp"
#include "td/core/streaming_session.hpp"
#include "td/core/tunnel_channel.hpp"
#include "td/crypto/credential_store.hpp"
#include "td/crypto/identity_store.hpp"
#include "td/crypto/secure_byte_channel.hpp"
#include "td/encode/encoder.hpp"
#include "td/usbmux/usbmux_client.hpp"
#include "td/vdisplay/virtual_display.hpp"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace {
constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kIdStart = 1, kIdStop = 2, kIdQuit = 3;

std::atomic<bool> g_running{false};
std::atomic<bool> g_quit{false};
std::mutex g_session_mutex;                       // guards g_session (worker writes, UI thread reads)
td::core::StreamingSession* g_session = nullptr;  // non-null only while the session is alive
NOTIFYICONDATAW g_nid{};                          // owned by the UI thread (NIM_ADD/DELETE)
std::thread g_worker;

// Cross-thread-safe tooltip update: build a local struct so we never race the UI thread's g_nid.
void SetStatus(HWND hwnd, const wchar_t* text) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = 1;
  nid.uFlags = NIF_TIP;
  wcsncpy_s(nid.szTip, text, _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

std::wstring ConfigPath() {
  wchar_t* appdata = nullptr;
  size_t len = 0;
  _wdupenv_s(&appdata, &len, L"APPDATA");
  std::wstring dir = appdata ? appdata : L".";
  if (appdata) free(appdata);
  dir += L"\\TetherDisplay";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\config.txt";
}

// Read the config via a wide path (so non-ASCII %APPDATA% works), then parse with the portable parser.
td::core::Config LoadConfigWide(const std::wstring& path) {
  std::ifstream f(path.c_str());  // MSVC accepts a wide path; portable ParseConfig does the rest
  if (!f) return td::core::Config{};
  std::stringstream ss;
  ss << f.rdbuf();
  return td::core::ParseConfig(ss.str());
}

// The streaming worker: identical loop to tools/stream, gated by g_running/g_quit.
void StreamLoop(HWND hwnd) {
  if (!td::crypto::InitCrypto()) {
    SetStatus(hwnd, L"TetherDisplay — crypto init failed");
    g_running = false;
    return;
  }
  const td::core::Config cfg = LoadConfigWide(ConfigPath());

  auto display = td::vdisplay::MakeParsecVirtualDisplay();
  td::vdisplay::VirtualDisplayInfo vdi;
  if (display->Add({2360, 1640, 60}, vdi) != td::vdisplay::VddStatus::Ok) {
    SetStatus(hwnd, L"TetherDisplay — virtual display failed");
    g_running = false;
    return;
  }
  td::crypto::CredentialManagerStore store;
  td::crypto::Identity me = td::crypto::LoadOrCreateIdentity(store);

  td::encode::EncoderConfig ec;
  ec.codec = cfg.use_hevc ? td::encode::Codec::Hevc : td::encode::Codec::H264;
  ec.width = vdi.active_mode.width;
  ec.height = vdi.active_mode.height;
  ec.fps = cfg.fps;
  ec.target_bitrate_kbps = cfg.target_bitrate_kbps;
  ec.max_bitrate_kbps = cfg.max_bitrate_kbps;

  while (g_running && !g_quit) {
    SetStatus(hwnd, L"TetherDisplay — waiting for iPad…");
    td::usbmux::UsbmuxClient mux;
    td::usbmux::AttachedDevice dev;
    if (mux.WaitForDevice(3000, dev) != td::usbmux::MuxStatus::Ok) continue;
    td::usbmux::Tunnel tunnel;
    if (mux.Connect(dev.device_id, cfg.device_port, tunnel) != td::usbmux::MuxStatus::Ok) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    td::core::TunnelChannel raw(tunnel);
    td::crypto::SecureByteChannel secure;
    if (secure.Establish(raw, me, td::crypto::Pairing::Role::Server, store) !=
        td::crypto::HandshakeResult::Ok) {
      SetStatus(hwnd, L"TetherDisplay — pairing refused/failed");
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    SetStatus(hwnd, L"TetherDisplay — streaming (paired)");
    auto capture = td::capture::MakeWgcCaptureSource();
    auto encoder = td::encode::MakeNvencEncoder();
    td::core::StreamingSession session(*capture, *encoder, secure, ec,
                                       td::capture::CaptureTarget{vdi.gdi_device_name});
    { std::lock_guard<std::mutex> lk(g_session_mutex); g_session = &session; }
    session.Run();
    // Clear under the lock BEFORE `session` (and its referenced locals) are destroyed at scope end.
    { std::lock_guard<std::mutex> lk(g_session_mutex); g_session = nullptr; }
  }
  display->Remove();
  SetStatus(hwnd, L"TetherDisplay — stopped");
}

void StartStreaming(HWND hwnd) {
  if (g_running.exchange(true)) return;       // already running
  if (g_worker.joinable()) g_worker.join();   // reap a previous worker before reassigning the thread
  g_worker = std::thread(StreamLoop, hwnd);
}

void StopStreaming() {
  g_running = false;
  { std::lock_guard<std::mutex> lk(g_session_mutex); if (g_session) g_session->Stop(); }
  if (g_worker.joinable()) g_worker.join();
}

void ShowMenu(HWND hwnd) {
  POINT pt;
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();
  const bool running = g_running.load();
  AppendMenuW(menu, MF_STRING | (running ? MF_GRAYED : 0), kIdStart, L"Start");
  AppendMenuW(menu, MF_STRING | (running ? 0 : MF_GRAYED), kIdStop, L"Stop");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kIdQuit, L"Quit");
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case kTrayMsg:
      if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) ShowMenu(hwnd);
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case kIdStart: StartStreaming(hwnd); break;
        case kIdStop: StopStreaming(); break;
        case kIdQuit:
          g_quit = true;
          StopStreaming();
          DestroyWindow(hwnd);  // routes through WM_DESTROY -> NIM_DELETE + PostQuitMessage
          break;
      }
      return 0;
    case WM_DESTROY:
      Shell_NotifyIconW(NIM_DELETE, &g_nid);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = L"TetherDisplayTray";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"TetherDisplay", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                            hInst, nullptr);

  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = kTrayMsg;
  g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wcscpy_s(g_nid.szTip, L"TetherDisplay — idle (right-click)");
  Shell_NotifyIconW(NIM_ADD, &g_nid);

  StartStreaming(hwnd);  // begin streaming immediately; the tray lets you Stop/Start/Quit

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  StopStreaming();
  return 0;
}

#else
int main() { return 0; }  // Windows-only tray app
#endif
