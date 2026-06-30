// Windows.Graphics.Capture of a single virtual display (M1.2), Windows only.
// Primary cross-adapter strategy: the capture D3D11 device is created on the ENCODER (NVIDIA) adapter,
// so DWM delivers frames already resident there (ready for NVENC) and performs any cross-adapter copy
// internally. See docs/ARCHITECTURE.md for the verified API sequence + sources.
//
// ⛔ Not compiled/tested in WSL (C++/WinRT + D3D11). Built on Windows via CMake; verified on hardware
//    by sandboxes/capture_sandbox.cpp. Points marked [VERIFY-ON-HW] need on-device confirmation.
#if defined(_WIN32)

#include "td/capture/adapter.hpp"
#include "td/capture/capture_source.hpp"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <atomic>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wgdx11 = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace td::capture {
namespace {

// ---- C++/WinRT <-> D3D interop helpers (canonical Kenny Kerr pattern) ----
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess
    : ::IUnknown {
  virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
};

wgdx11::IDirect3DDevice WrapDevice(ID3D11Device* d3d) {
  ComPtr<IDXGIDevice> dxgi;
  if (FAILED(d3d->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf())))) return nullptr;
  winrt::com_ptr<::IInspectable> inspectable;
  if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()))) return nullptr;
  return inspectable.try_as<wgdx11::IDirect3DDevice>();  // null (not throw) on failure
}

ComPtr<ID3D11Texture2D> TextureFromSurface(wgdx11::IDirect3DSurface const& surface) {
  auto access = surface.as<IDirect3DDxgiInterfaceAccess>();
  ComPtr<ID3D11Texture2D> tex;
  access->GetInterface(IID_PPV_ARGS(tex.GetAddressOf()));
  return tex;
}

AdapterId ToAdapterId(const LUID& luid) {
  return AdapterId{luid.LowPart, luid.HighPart};
}

struct AdapterEntry {
  AdapterInfo info;
  ComPtr<IDXGIAdapter1> adapter;
};

std::vector<AdapterEntry> EnumAdapters(IDXGIFactory1* factory) {
  std::vector<AdapterEntry> out;
  ComPtr<IDXGIAdapter1> a;
  for (UINT i = 0; factory->EnumAdapters1(i, a.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d{};
    if (FAILED(a->GetDesc1(&d))) continue;
    if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;  // skip WARP — can't share/encode
    AdapterEntry e;
    e.info.id = ToAdapterId(d.AdapterLuid);
    e.info.vendor_id = d.VendorId;
    e.info.dedicated_vram = d.DedicatedVideoMemory;
    e.adapter = a;
    out.push_back(std::move(e));
  }
  return out;
}

// Resolve the capture display's HMONITOR and the LUID of the adapter that drives it, by matching the
// GDI device name carried in DXGI_OUTPUT_DESC.
bool FindMonitor(const std::vector<AdapterEntry>& adapters, const std::wstring& device_name,
                 HMONITOR& out_monitor, AdapterId& out_capture_luid) {
  for (const auto& e : adapters) {
    ComPtr<IDXGIOutput> output;
    for (UINT j = 0; e.adapter->EnumOutputs(j, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND;
         ++j) {
      DXGI_OUTPUT_DESC od{};
      if (FAILED(output->GetDesc(&od))) continue;
      if (device_name == od.DeviceName) {
        out_monitor = od.Monitor;
        out_capture_luid = e.info.id;
        return true;
      }
    }
  }
  return false;
}

class WgcCaptureSource final : public ICaptureSource {
 public:
  ~WgcCaptureSource() override { Stop(); }

  CaptureStatus Start(const CaptureTarget& target, FrameCallback on_frame) override {
    Stop();  // idempotent re-Start

    // COM apartment MUST be initialized before any WinRT activation (IsSupported/CreateForMonitor
    // resolve activation factories). On a fresh headless thread this would otherwise throw
    // CO_E_NOTINITIALIZED. The whole body is then wrapped so WinRT throws become typed statuses.
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) { /* already initialized in a compatible mode */ }

    try {
      if (!wgc::GraphicsCaptureSession::IsSupported()) return CaptureStatus::WgcUnavailable;

      ComPtr<IDXGIFactory1> factory;
      if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
        return CaptureStatus::DeviceCreateFailed;
      auto adapters = EnumAdapters(factory.Get());
      if (adapters.empty()) return CaptureStatus::DeviceCreateFailed;

      // Choose the encoder (NVIDIA) adapter and create the capture device on it (primary strategy).
      std::vector<AdapterInfo> infos;
      infos.reserve(adapters.size());
      for (auto& e : adapters) infos.push_back(e.info);
      auto encoder = SelectEncoderAdapter(infos);
      if (!encoder) return CaptureStatus::DeviceCreateFailed;
      encoder_luid_ = encoder->id;

      ComPtr<IDXGIAdapter1> encoder_adapter;
      for (auto& e : adapters)
        if (e.info.id == encoder->id) encoder_adapter = e.adapter;

      HMONITOR monitor = nullptr;
      if (!FindMonitor(adapters, target.gdi_device_name, monitor, capture_luid_))
        return CaptureStatus::MonitorNotFound;
      crossed_adapters_ = NeedsCrossAdapterCopy(capture_luid_, encoder_luid_);

      const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
      if (FAILED(D3D11CreateDevice(encoder_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                                   nullptr, 0, D3D11_SDK_VERSION, d3d_.ReleaseAndGetAddressOf(),
                                   nullptr, ctx_.ReleaseAndGetAddressOf())))
        return CaptureStatus::DeviceCreateFailed;

      winrt_device_ = WrapDevice(d3d_.Get());
      if (!winrt_device_) return CaptureStatus::DeviceCreateFailed;

      auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                                                   ::IGraphicsCaptureItemInterop>();
      winrt::check_hresult(interop->CreateForMonitor(
          monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item_)));

      const auto size = item_.Size();
      EnsureDeliveryTexture(static_cast<std::uint32_t>(size.Width),
                            static_cast<std::uint32_t>(size.Height));  // seed: no first-frame recreate
      pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
          winrt_device_, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
      session_ = pool_.CreateCaptureSession(item_);
      cb_ = std::move(on_frame);
      frame_arrived_ = pool_.FrameArrived(
          winrt::auto_revoke, {this, &WgcCaptureSource::OnFrameArrived});
      capturing_ = true;          // set before StartCapture so an immediate frame is accepted
      session_.StartCapture();
      return CaptureStatus::Ok;
    } catch (winrt::hresult_error const&) {
      Stop();
      return CaptureStatus::StartFailed;
    }
  }

  void Stop() override {
    capturing_ = false;       // make any handler that beats us to the lock bail out
    frame_arrived_.revoke();  // unsubscribe (does NOT drain a handler already running)
    // Hold frame_mutex_ so an in-flight OnFrameArrived finishes before we free the state it uses,
    // and none can start (it returns on !capturing_). The frame callback therefore MUST NOT call
    // Stop() or destroy this source — it runs under this lock and would self-deadlock.
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (session_) { session_.Close(); session_ = nullptr; }
    if (pool_) { pool_.Close(); pool_ = nullptr; }
    item_ = nullptr;
    winrt_device_ = nullptr;
    cb_ = nullptr;
    delivery_.Reset();
    delivery_w_ = delivery_h_ = 0;
    ctx_.Reset();
    d3d_.Reset();
  }

  bool IsCapturing() const override { return capturing_; }

  void* GetDevice() const override { return d3d_.Get(); }

 private:
  void OnFrameArrived(wgc::Direct3D11CaptureFramePool const& sender,
                      winrt::Windows::Foundation::IInspectable const&) {
    // Hold the lock for the whole handler: it serializes against Stop()'s teardown, so pool_/ctx_/
    // delivery_ are guaranteed alive here once the capturing_ check passes.
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (!capturing_) return;  // stopped or tearing down — do not touch freed state

    auto frame = sender.TryGetNextFrame();
    if (!frame) return;  // pool can fire spuriously
    auto src = TextureFromSurface(frame.Surface());
    if (!src) return;

    const auto content = frame.ContentSize();
    const auto w = static_cast<std::uint32_t>(content.Width);
    const auto h = static_cast<std::uint32_t>(content.Height);

    // Recreate the pool when the display resolution changes (per the official sample).
    if (w != delivery_w_ || h != delivery_h_) {
      EnsureDeliveryTexture(w, h);
      try {
        pool_.Recreate(winrt_device_, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                       {static_cast<int32_t>(w), static_cast<int32_t>(h)});
      } catch (winrt::hresult_error const&) { /* keep delivering at the old size */ }
    }
    if (!delivery_) return;

    // Copy out of the recycled pool texture into our stable delivery texture before the next frame.
    ctx_->CopyResource(delivery_.Get(), src.Get());

    CapturedFrame out;
    out.encoder_texture = delivery_.Get();
    out.width = w;
    out.height = h;
    out.format = CaptureFormat::Bgra8;
    out.frame_id = frame_id_++;
    out.timestamp_100ns = static_cast<std::uint64_t>(frame.SystemRelativeTime().count());
    out.crossed_adapters = crossed_adapters_;
    if (cb_) cb_(out);
  }

  void EnsureDeliveryTexture(std::uint32_t w, std::uint32_t h) {
    delivery_.Reset();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;  // NVENC registers this as input in M1.3
    if (SUCCEEDED(d3d_->CreateTexture2D(&d, nullptr, delivery_.GetAddressOf()))) {
      delivery_w_ = w;
      delivery_h_ = h;
    }
  }

  ComPtr<ID3D11Device> d3d_;
  ComPtr<ID3D11DeviceContext> ctx_;
  wgdx11::IDirect3DDevice winrt_device_{nullptr};
  wgc::GraphicsCaptureItem item_{nullptr};
  wgc::Direct3D11CaptureFramePool pool_{nullptr};
  wgc::GraphicsCaptureSession session_{nullptr};
  wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frame_arrived_;
  FrameCallback cb_;

  ComPtr<ID3D11Texture2D> delivery_;
  std::uint32_t delivery_w_ = 0, delivery_h_ = 0;
  std::mutex frame_mutex_;
  std::atomic<bool> capturing_{false};
  std::uint64_t frame_id_ = 0;
  AdapterId capture_luid_, encoder_luid_;
  bool crossed_adapters_ = false;
};

}  // namespace

std::unique_ptr<ICaptureSource> MakeWgcCaptureSource() {
  return std::make_unique<WgcCaptureSource>();
}

}  // namespace td::capture

#endif  // _WIN32
