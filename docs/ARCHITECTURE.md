# TetherDisplay Architecture

Expanded from the README's §1 picture. There is no native "display input" on an iPad — this is a
low-latency **compressed-video streaming pipeline over USB**:

```
[ Windows host ]                                            [ iPad ]
 IddCx virtual display (M1.1, Parsec VDD)
   → WGC capture of THAT display only (M1.2)
   → NVENC HEVC/H.264 encode, low-latency (M1.3/M4)
   → §5 protocol framing (M0) over usbmux (M2)
        → Apple Mobile Device Service ══ USB-C ══►  PeerTalk
                                                     → VideoToolbox decode
                                                     → Metal render (native 2360×1640)
```

Local-only: no Wi-Fi/LAN/cloud/telemetry, ever. Control + video share one tunneled socket as two
logical channels (§5). Components are split so each is independently testable: see the per-module
folders under `windows-host/` and `ios-client/`.

---

## Capture design (M1.2) — verified against Microsoft/NVIDIA docs

The capture path is the project's trickiest host piece because the Dell XPS is **hybrid-GPU**: the
desktop/virtual display may be composited on the Intel iGPU while NVENC lives on the RTX 4080. A
texture on one adapter is not directly usable by an encoder on another (§6.4).

**Primary strategy (chosen): capture onto the encoder adapter.** Create the WGC capture `ID3D11Device`
on the **NVIDIA** adapter. Windows.Graphics.Capture / DWM then deliver each frame as a texture already
resident on the RTX 4080 — the runtime performs any cross-adapter copy internally, and
`frame.Surface()` yields an NVENC-ready `ID3D11Texture2D`. This avoids a fragile manual cross-adapter
path for the common case.

**Fallback (implemented, `cross_adapter.*`): explicit keyed-mutex shared texture.** If capturing
directly on the encoder adapter proves unreliable on this hardware, use an NT-handle keyed-mutex
shared texture: source on the capture device with
`D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`,
`IDXGIResource1::CreateSharedHandle`, `ID3D11Device1::OpenSharedResource1` on the NVIDIA device,
serialized with `IDXGIKeyedMutex::AcquireSync/ReleaseSync` and a producer-side `Flush`.
Note (doc-verified): **there is no `D3D11_RESOURCE_MISC_SHARED_CROSS_ADAPTER`** — explicit
cross-adapter resources are a D3D12 concept (`D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER`); the authoritative
"GPU copies, no CPU marshaling" path is the D3D12 cross-adapter shared heap, kept as a last resort.
**No zero-copy exists across separate VRAM — every frame crosses PCIe regardless.** Keyed-mutex sharing
across two different-IHV adapters is historically driver-finicky → `[VERIFY-ON-HW]`.

### Verified API sequence (WGC, C++/WinRT)
1. `D3D11CreateDevice` on the encoder `IDXGIAdapter1` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`).
2. `CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, &inspectable)` → `.as<IDirect3DDevice>()`.
3. Resolve the virtual display's `HMONITOR` + its driving adapter LUID from `DXGI_OUTPUT_DESC`
   (`DeviceName` matches M1.1's `\\.\DISPLAYn`; `Monitor` is the HMONITOR).
4. `get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>()` →
   `CreateForMonitor(hmon, guid_of<GraphicsCaptureItem>(), put_abi(item))`.
5. `Direct3D11CaptureFramePool::CreateFreeThreaded(device, B8G8R8A8UIntNormalized, 2, item.Size())`
   (free-threaded: needed for a headless host with no message pump).
6. `pool.CreateCaptureSession(item)`; subscribe `FrameArrived`; `session.StartCapture()`.
7. In `FrameArrived`: `TryGetNextFrame()` (may be null) → `frame.Surface()` →
   `IDirect3DDxgiInterfaceAccess::GetInterface(IID_ID3D11Texture2D)`. Copy out before the next frame
   (the pool recycles the texture); `Recreate(...)` on `ContentSize()` change.

### Pitfalls guarded in code
- Gate on `GraphicsCaptureSession::IsSupported()` (needs Win10 1903+).
- `CreateForMonitor` can return `E_ACCESSDENIED` in restricted/elevated contexts.
- WGC output is BGRA8; NVENC prefers NV12 → a color-convert stage is added in M1.3 (not M1.2).
- Shared resources must be 2D, single-mip, `USAGE_DEFAULT`, non-MSAA, no CPU-access flags; WARP/REF
  devices can't share. Keyed-mutex: only key 0 is accepted initially; check `WAIT_ABANDONED`.

### Sources (fetched 2026-06-29)
- WGC monitor capture: <https://learn.microsoft.com/windows/uwp/audio-video-camera/screen-capture-video>,
  `IGraphicsCaptureItemInterop::CreateForMonitor`, `CreateDirect3D11DeviceFromDXGIDevice` (MS Learn),
  Microsoft `Windows.UI.Composition-Win32-Samples` (ScreenCapture), Kenny Kerr interop gist.
- Cross-adapter D3D11: `D3D11_RESOURCE_MISC_FLAG`, `IDXGIResource1::CreateSharedHandle`,
  `ID3D11Device1::OpenSharedResource1`, `IDXGIKeyedMutex::AcquireSync`,
  `direct3darticles/surface-sharing-between-windows-graphics-apis`, `direct3d12/shared-heaps` (MS Learn).
- Enumeration: `DXGI_OUTPUT_DESC`, `IDXGIFactory1::EnumAdapters1`, `IDXGIAdapter1::GetDesc1` (MS Learn).
