// Cross-adapter keyed-mutex texture bridge (§6.4 fallback). Windows only. See cross_adapter.hpp.
// ⛔ Not compiled/tested in WSL (pure Win32/D3D11). Built on Windows; exercised by the capture sandbox
//    in --test-cross-adapter mode. Keyed-mutex behavior across iGPU/dGPU is [VERIFY-ON-HW].
#if defined(_WIN32)

#include "td/capture/cross_adapter.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace td::capture {

// Keyed-mutex keys ping-pong: the producer takes the surface at key 0 and releases it at key 1; the
// consumer takes it at key 1 and releases it back to key 0 for the next frame.
namespace {
constexpr UINT64 kKeyProducer = 0;
constexpr UINT64 kKeyConsumer = 1;

// AcquireSync returns S_OK on success, or WAIT_ABANDONED if the previous owner died — in BOTH cases we
// now hold the mutex and must release it (the contents may be stale on WAIT_ABANDONED). Any other
// value (e.g. WAIT_TIMEOUT, a failure HRESULT) means we did NOT acquire. Treating WAIT_ABANDONED as a
// failure and returning early would leave the surface owned and deadlock every later acquire.
bool Acquired(HRESULT hr) { return hr == S_OK || hr == static_cast<HRESULT>(WAIT_ABANDONED); }
}  // namespace

struct CrossAdapterBridge::Impl {
  ComPtr<ID3D11Texture2D> producer_tex;   // lives on the producer device
  ComPtr<IDXGIKeyedMutex> producer_mutex;
  ComPtr<ID3D11Texture2D> consumer_tex;   // same surface, opened on the consumer device
  ComPtr<IDXGIKeyedMutex> consumer_mutex;
  bool ready = false;
};

CrossAdapterBridge::CrossAdapterBridge() : impl_(new Impl) {}
CrossAdapterBridge::~CrossAdapterBridge() { delete impl_; }
bool CrossAdapterBridge::valid() const { return impl_ && impl_->ready; }

bool CrossAdapterBridge::Init(ID3D11Device* producer, ID3D11Device* consumer, std::uint32_t width,
                              std::uint32_t height) {
  if (!producer || !consumer) return false;

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // broadly shareable (Tier 0); matches WGC output
  desc.SampleDesc.Count = 1;                 // shared resources must be non-MSAA
  desc.Usage = D3D11_USAGE_DEFAULT;          // required for sharing; no CPU-access flags allowed
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  if (FAILED(producer->CreateTexture2D(&desc, nullptr, impl_->producer_tex.GetAddressOf())))
    return false;
  if (FAILED(impl_->producer_tex.As(&impl_->producer_mutex))) return false;

  ComPtr<IDXGIResource1> resource;
  if (FAILED(impl_->producer_tex.As(&resource))) return false;
  HANDLE shared = nullptr;
  if (FAILED(resource->CreateSharedHandle(
          nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared)))
    return false;

  ComPtr<ID3D11Device1> consumer1;
  HRESULT hr = consumer->QueryInterface(IID_PPV_ARGS(consumer1.GetAddressOf()));
  if (SUCCEEDED(hr)) {
    hr = consumer1->OpenSharedResource1(shared, IID_PPV_ARGS(impl_->consumer_tex.GetAddressOf()));
  }
  if (shared) CloseHandle(shared);  // the opened resource keeps its own reference
  if (FAILED(hr)) return false;
  if (FAILED(impl_->consumer_tex.As(&impl_->consumer_mutex))) return false;

  impl_->ready = true;
  return true;
}

bool CrossAdapterBridge::Produce(ID3D11DeviceContext* producer_ctx, ID3D11Texture2D* src) {
  if (!valid() || !producer_ctx || !src) return false;
  // Initially the surface is unowned and only accepts key 0.
  if (!Acquired(impl_->producer_mutex->AcquireSync(kKeyProducer, INFINITE))) return false;
  producer_ctx->CopyResource(impl_->producer_tex.Get(), src);
  producer_ctx->Flush();  // required after updating a shared texture, else consumer reads stale data
  return impl_->producer_mutex->ReleaseSync(kKeyConsumer) == S_OK;
}

bool CrossAdapterBridge::Consume(ID3D11DeviceContext* consumer_ctx, ID3D11Texture2D* dst) {
  if (!valid() || !consumer_ctx || !dst) return false;
  if (!Acquired(impl_->consumer_mutex->AcquireSync(kKeyConsumer, INFINITE))) return false;
  consumer_ctx->CopyResource(dst, impl_->consumer_tex.Get());  // driver does the cross-adapter DMA
  return impl_->consumer_mutex->ReleaseSync(kKeyProducer) == S_OK;
}

}  // namespace td::capture

#endif  // _WIN32
