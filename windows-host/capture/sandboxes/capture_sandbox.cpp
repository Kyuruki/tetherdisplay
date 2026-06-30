// M1.2 hardware sandbox (R5) — Windows console app the OPERATOR runs on the XPS.
//
//   td_capture_sandbox "\\.\DISPLAY3"        capture that display ~3s; save first frame to frame.bmp
//   td_capture_sandbox --test-cross-adapter  round-trip a texture iGPU<->dGPU and verify pixels
//
// The first mode is the M1.2 gate: open frame.bmp and confirm it shows the virtual second display.
// The second proves the §6.4 keyed-mutex bridge works on this specific hardware. The agent cannot run
// either (no GPU/driver access).
#if defined(_WIN32)

#include "td/capture/capture_source.hpp"
#include "td/capture/cross_adapter.hpp"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace td::capture;

namespace {

// Copy a BGRA8 GPU texture to CPU and write a 32-bpp BMP. Returns false on failure.
bool SaveBgraTextureToBmp(ID3D11Texture2D* tex, const char* path) {
  ComPtr<ID3D11Device> dev;
  tex->GetDevice(dev.GetAddressOf());
  ComPtr<ID3D11DeviceContext> ctx;
  dev->GetImmediateContext(ctx.GetAddressOf());

  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(dev->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) return false;
  ctx->CopyResource(staging.Get(), tex);

  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;

  const int w = static_cast<int>(desc.Width), h = static_cast<int>(desc.Height);
  const uint32_t row = static_cast<uint32_t>(w) * 4;
  const uint32_t imgSize = row * h;
#pragma pack(push, 1)
  struct { uint16_t bfType; uint32_t bfSize; uint16_t r1, r2; uint32_t bfOffBits; } fh{
      0x4D42, 54 + imgSize, 0, 0, 54};
  struct { uint32_t size; int32_t w, h; uint16_t planes, bpp; uint32_t comp, img;
           int32_t xppm, yppm; uint32_t clrUsed, clrImp; } ih{40, w, -h, 1, 32, 0, imgSize, 0, 0, 0, 0};
#pragma pack(pop)
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  f.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
  for (int y = 0; y < h; ++y)
    f.write(reinterpret_cast<const char*>(map.pData) + static_cast<size_t>(y) * map.RowPitch, row);
  ctx->Unmap(staging.Get(), 0);
  return true;
}

int RunCapture(const wchar_t* device_name) {
  auto source = MakeWgcCaptureSource();
  if (!source) { std::fprintf(stderr, "MakeWgcCaptureSource returned null\n"); return 2; }

  std::atomic<uint64_t> frames{0};
  std::atomic<bool> saved{false};
  bool crossed = false;
  uint32_t cw = 0, ch = 0;

  CaptureTarget target{device_name};
  auto st = source->Start(target, [&](const CapturedFrame& f) {
    cw = f.width; ch = f.height; crossed = f.crossed_adapters;
    if (!saved.exchange(true))
      SaveBgraTextureToBmp(static_cast<ID3D11Texture2D*>(f.encoder_texture), "frame.bmp");
    frames.fetch_add(1);
  });
  if (st != CaptureStatus::Ok) {
    std::fprintf(stderr, "Start failed: %s\n", to_string(st));
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::seconds(3));
  source->Stop();

  const uint64_t n = frames.load();
  std::printf("Captured %llu frames at %ux%u (~%.1f fps). crossed_adapters=%s. Wrote frame.bmp=%s\n",
              static_cast<unsigned long long>(n), cw, ch, n / 3.0, crossed ? "yes" : "no",
              saved.load() ? "yes" : "no");
  std::printf(">>> Open frame.bmp and confirm it shows the VIRTUAL second display's contents.\n");
  return n > 0 ? 0 : 1;
}

int RunCrossAdapterTest() {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return 2;
  std::vector<ComPtr<IDXGIAdapter1>> adapters;
  ComPtr<IDXGIAdapter1> a;
  for (UINT i = 0; factory->EnumAdapters1(i, a.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d{};
    a->GetDesc1(&d);
    if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) adapters.push_back(a);
  }
  if (adapters.size() < 2) {
    std::printf("Need >=2 hardware adapters to test cross-adapter; found %zu. Skipping.\n",
                adapters.size());
    return 0;
  }

  auto makeDevice = [](IDXGIAdapter1* ad, ComPtr<ID3D11Device>& dev, ComPtr<ID3D11DeviceContext>& ctx) {
    return SUCCEEDED(D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                       dev.GetAddressOf(), nullptr, ctx.GetAddressOf()));
  };
  ComPtr<ID3D11Device> pd, cd;
  ComPtr<ID3D11DeviceContext> pctx, cctx;
  if (!makeDevice(adapters[0].Get(), pd, pctx) || !makeDevice(adapters[1].Get(), cd, cctx)) return 2;

  const UINT W = 64, H = 64;
  std::vector<uint32_t> pattern(W * H);
  for (UINT i = 0; i < W * H; ++i) pattern[i] = 0xFF000000u | (i * 2654435761u);  // deterministic

  D3D11_TEXTURE2D_DESC sd{};
  sd.Width = W; sd.Height = H; sd.MipLevels = 1; sd.ArraySize = 1;
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1; sd.Usage = D3D11_USAGE_DEFAULT;
  sd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init{pattern.data(), W * 4, 0};
  ComPtr<ID3D11Texture2D> src;
  if (FAILED(pd->CreateTexture2D(&sd, &init, src.GetAddressOf()))) return 2;

  CrossAdapterBridge bridge;
  if (!bridge.Init(pd.Get(), cd.Get(), W, H)) {
    std::printf("CrossAdapterBridge.Init FAILED on this hardware (keyed-mutex share unsupported "
                "across these adapters). The primary path captures directly on the encoder adapter "
                "and does not need this; see docs/ARCHITECTURE.md.\n");
    return 1;
  }

  D3D11_TEXTURE2D_DESC dd = sd;
  dd.Usage = D3D11_USAGE_STAGING; dd.BindFlags = 0; dd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> dst;
  if (FAILED(cd->CreateTexture2D(&dd, nullptr, dst.GetAddressOf()))) return 2;

  if (!bridge.Produce(pctx.Get(), src.Get()) || !bridge.Consume(cctx.Get(), dst.Get())) {
    std::printf("Produce/Consume FAILED.\n");
    return 1;
  }

  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(cctx->Map(dst.Get(), 0, D3D11_MAP_READ, 0, &map))) return 2;
  bool ok = true;
  for (UINT y = 0; y < H && ok; ++y) {
    const auto* rowp = reinterpret_cast<const uint32_t*>(
        static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch);
    if (std::memcmp(rowp, &pattern[y * W], W * 4) != 0) ok = false;
  }
  cctx->Unmap(dst.Get(), 0);
  std::printf("Cross-adapter round-trip pixel check: %s\n", ok ? "PASS" : "MISMATCH");
  return ok ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 2 && std::wcscmp(argv[1], L"--test-cross-adapter") == 0) return RunCrossAdapterTest();
  if (argc >= 2) return RunCapture(argv[1]);
  std::printf("usage: td_capture_sandbox \"\\\\.\\DISPLAY3\" | --test-cross-adapter\n");
  return 64;
}

#else
int main() { return 0; }  // sandbox is Windows-only
#endif
