# TetherDisplay RUNBOOK (operator steps the agent cannot run)

Copy-pasteable, ordered steps the human performs on the hardware. Covers **M1.1 → M6** below — work
through them in order; the final "cold-start dry run" (M6) is the project's Definition of Done. (M0 needs
no operator steps; build/test it with `windows-host/transport/protocol/Makefile` on WSL or via CMake on
Windows.)

---

## M1.1 — Bring up the Parsec virtual display

Goal/gate: a **second display at (or nearest to) 2360×1640** appears in Windows Display Settings when
you run the sandbox, and disappears when you exit it.

### 1. Install the Parsec Virtual Display Driver (Option A1)

The driver is Parsec's proprietary, **pre-signed** IddCx driver (no self-signing needed). You can
install it without the full Parsec app:

1. Download a Parsec VDD release per the reference project's instructions:
   <https://github.com/nomi-san/parsec-vdd> (it points to Parsec's official signed driver builds).
2. Install the driver (the reference repo documents using `nefconw` / `pnputil`). Confirm it appears
   in **Device Manager → Display adapters** as the Parsec Virtual Display Adapter (hardware id
   `Root\Parsec\VDA`).

### 2. (For native resolution) register 2360×1640 as a custom resolution

The driver exposes a fixed preset list (720p…4K) plus up to **5** custom resolutions read from the
registry **before** a display connects. To get the iPad-native mode:

1. As Administrator, add `2360 × 1640 @ 60` under `HKLM\SOFTWARE\Parsec\vdd`.
   **[VERIFY]** Use Parsec's official custom-resolution guide for the exact value names/format
   (linked from the repo's `docs/PARSEC_VDD_SPECS.md`) — do not guess the schema.
2. If you skip this step, the sandbox still works but applies the **nearest** preset (it will say so),
   and the host will render at that mode (the iPad can scale).

### 3. Build the host (M1.1 targets)

Prerequisites: Visual Studio 2022 (MSVC, C++20), CMake ≥3.20, Windows 11 SDK.

```
cmake -S . -B build
cmake --build build --config Debug --target td_vdd_sandbox
```

(`ctest --test-dir build -C Debug -R "VddProtocol|PickBestMode|VddStatus|Vectors|Errors|Parser|Version|RoundTrip"`
runs the portable unit tests for the protocol + vdisplay logic; the driver path is exercised by the sandbox.)

### 4. Run the sandbox and verify (the gate)

Run **as Administrator** (display + driver IOCTLs):

```
build\windows-host\vdisplay\Debug\td_vdd_sandbox.exe   (path may vary by generator)
```

Expected:
- It prints the driver index, the GDI device name (e.g. `\\.\DISPLAY3`), and the applied mode.
- **Settings → System → Display** shows a new second display; you can drag a window onto it.
- Press **Enter** → the second display disappears.

### 5. Troubleshooting

- `Add failed: DriverNotFound` → driver not installed / not present (step 1).
- `Add failed: DriverNotRunning` → device present but the version IOCTL failed; re-install / reboot.
- `Add failed: ModeUnavailable` or "nearest available" → 2360×1640 not registered (step 2).
- Second display never appears though the tool says OK → confirm the new `\\.\DISPLAYn` in Display
  Settings; check Device Manager for the Parsec adapter. **[VERIFY-ON-HW]** the new-monitor detection
  (set-difference) on your machine.

---

## M1.2 — Capture the virtual display

Goal/gate: `frame.bmp` written by the capture sandbox shows the **virtual display's** contents (not
your primary screen), captured onto the NVIDIA adapter.

### 1. Build

```
cmake --build build --config Debug --target td_capture_sandbox
```

### 2. Capture the virtual display (the gate)

Use the `\\.\DISPLAYn` device name that the **M1.1** sandbox printed for the virtual display:

```
build\windows-host\capture\Debug\td_capture_sandbox.exe "\\.\DISPLAY3"
```

(With the M1.1 virtual display active — run `td_vdd_sandbox` in another window, or wire them together
later.) Expected: it prints a frame count, the resolution, `crossed_adapters=yes|no`, and writes
`frame.bmp`. **Open `frame.bmp`** — it must show the virtual second display's contents. Put a
recognizable window on that display first so you can tell it apart from the primary.

### 3. (Optional) prove the cross-adapter bridge on this hardware

```
build\windows-host\capture\Debug\td_capture_sandbox.exe --test-cross-adapter
```

Expected: `Cross-adapter round-trip pixel check: PASS`. If it prints `Init FAILED`, the keyed-mutex
share isn't supported across your iGPU/dGPU — that's fine: the primary path captures directly on the
NVIDIA adapter and doesn't use this bridge (it exists only as a fallback). **[VERIFY-ON-HW]**

### 4. Troubleshooting

- `Start failed: WgcUnavailable` → Windows.Graphics.Capture needs Win10 1903+; update Windows.
- `Start failed: MonitorNotFound` → the `\\.\DISPLAYn` you passed isn't attached; re-check the M1.1
  output (the index can change between sessions).
- `Start failed: DeviceCreateFailed` → no NVIDIA adapter found, or D3D11 device creation failed.
- `frame.bmp` shows the wrong/black screen → DRM content is expected to be black (LIMITATIONS); confirm
  you pointed at the virtual display's device name, not the primary.
- `crossed_adapters=yes` is informational: it means the virtual display is composited on a different
  GPU than NVENC and DWM is doing the cross-adapter copy for you. That's expected and fine.

---

## M1.3 — NVENC encode-to-file (the M1 gate)

Goal/gate (completes M1): record a few seconds of the virtual display to a `.h265` file and play it
back — it must show that desktop at native resolution.

### 1. Prerequisites

- **NVIDIA driver** (GeForce/Studio) — ships `nvEncodeAPI64.dll`; keep it current.
- **NVIDIA Video Codec SDK** — download from NVIDIA, note the `Interface/` folder (contains
  `nvEncodeAPI.h`). No `.lib` is needed (the DLL is loaded at runtime).
- A player: `ffplay`/`ffmpeg` or VLC.

### 2. Build

```
cmake -S . -B build -DNVENC_INCLUDE_DIR="C:/path/to/Video_Codec_SDK/Interface"
cmake --build build --config Debug --target td_encode_sandbox
```

If `NVENC_INCLUDE_DIR` is unset, CMake skips `td_encode`/`td_encode_sandbox` (and says so) but still
builds the portable encode tests.

### 3. Record (the gate)

With the M1.1 virtual display active, using its `\\.\DISPLAYn` from the M1.1 sandbox:

```
build\windows-host\encode\Debug\td_encode_sandbox.exe "\\.\DISPLAY3" capture.h265 5
```

It prints frames/packets/bytes/keyframes. Then **play it back**:

```
ffplay capture.h265        (or open in VLC)
```

**Pass:** the video shows the virtual second display's contents at 2360×1640 (or the nearest mode),
30+ fps. Put a moving window on that display while recording so playback is obviously live.

### 4. Troubleshooting

- `encoder Initialize failed: SdkNotFound` → `nvEncodeAPI64.dll` missing/old; update the NVIDIA driver.
- `SessionInitFailed` → the GPU/driver doesn't support the requested HEVC low-latency config; try the
  H.264 path (set `cfg.codec = Codec::H264`) or a newer driver.
- File won't open in a player → it's a raw HEVC elementary stream; `repeatSPSPPS=1` inlines VPS/SPS/PPS,
  but some players want a container: `ffmpeg -i capture.h265 -c copy capture.mp4`.
- Black video → DRM-protected content renders black on the virtual display (expected; see LIMITATIONS).
- Very large file / stutter → bitrate too high for later USB streaming; it's capped at ~80 Mbps but you
  can lower `target_bitrate_kbps`.

---

## M2 — usbmux transport, host→device bytes

Goal/gate: the iPad gate app shows the **same byte count + checksum** the host streamed over the USB
cable. No video yet — this proves the byte pipe.

### 1. Host prerequisites

- **Apple Mobile Device Support** installed (ships with iTunes; can be extracted like Duet does). This
  provides the **Apple Mobile Device Service** the host talks to at `127.0.0.1:27015` **[VERIFY]** the
  port on your machine).
- Plug in the iPad and **Trust** the computer.

### 2. Build the host

```
cmake --build build --config Debug --target td_usbmux_sandbox
ctest --test-dir build -C Debug -R "Usbmux|BuildListen|BuildConnect|PlistGet|ParseResult|ParseAttached"
```

(The `UsbmuxClientMock` integration test runs the full Listen→Connect→tunnel flow against an in-process
mock daemon — it already passes in CI/WSL, no device needed.)

### 3. Build + launch the iPad gate app

Build `ios-client/M2Gate/GateListener.swift` on a Mac (see `ios-client/README.md`), sideload via
AltStore, and **launch it** — it listens on TCP port **2345** and shows "Waiting for host".

### 4. Stream bytes (the gate)

```
build\windows-host\transport\usbmux\Debug\td_usbmux_sandbox.exe 2345 64
```

It prints e.g. `Sent 65536 bytes, FNV-1a checksum = 0x????????`.
**Pass:** the iPad app shows the **same** byte count and checksum.

### 5. Troubleshooting

- `WaitForDevice failed: Timeout` → Apple Mobile Device Service not running, or the iPad isn't
  plugged in / trusted. Confirm the service (and `[VERIFY]` it listens on 27015).
- `Connect ... ResultConnRefused` → the iPad gate app isn't listening (launch it) or the port doesn't
  match (host arg vs `gatePort` in GateListener.swift, default 2345).
- `Connect ... ResultBadDevice` → the DeviceID went away (replug); re-run.
- Checksums differ → bytes were corrupted/dropped — capture the host vs device totals and report.

### AltStore sideload + 7-day refresh (run from the Windows host)

Install AltServer on the XPS, install the `.ipa` to the iPad, and confirm auto-refresh is configured
(free provisioning profiles expire every 7 days; AltStore re-signs). **Any iOS code change requires
rebuilding the `.ipa` on the Mac** — AltStore only refreshes the existing signature.

---

## M3 — decode + render a single keyframe (the M3 gate)

Goal/gate: one real captured frame from the host appears on the iPad. Also freezes §5 (both endpoints
now pass `protocol/test-vectors/`).

### 1. iOS app (build on Mac)

Use the M3 SwiftUI app target containing `Transport/WireProtocol.swift`, `Decode/HEVCDecoder.swift`,
`Render/MetalVideoView.swift`, `Render/Shaders.metal`, `Session/VideoSession.swift` with `M3ContentView`
as the root view. Add `Tests/WireProtocolTests.swift` to a test target and run it — it validates the
Swift codec against the shared `protocol/test-vectors/` (the §5 freeze check). Sideload + launch; it
listens on port 2345 and shows "Waiting for host".

### 2. Host (Windows)

Build the full-pipeline sandbox (needs the virtual display driver + NVENC + Apple Mobile Device Service):

```
cmake --build build --config Debug --target td_stream_keyframe
build\tools\stream_keyframe\Debug\td_stream_keyframe.exe 2345
```

It brings up the virtual display, captures one frame, NVENC-encodes it as an IDR, frames it as a §5
VIDEO_FRAME, and sends it over usbmux.

### 3. Verify (the gate)

The iPad shows the captured keyframe (drag a recognizable window onto the virtual display first). The
status line shows "Rendering W×H" and frames decoded ≥ 1.

### 4. Troubleshooting

- iPad stays "Waiting for host" → the host couldn't connect (check `td_stream_keyframe` output;
  ResultConnRefused = the app isn't listening / wrong port).
- "HEVC HW decode unsupported" → every iPadOS-18 device supports it; check the build/runtime.
- Garbled/wrong colors → BT.709 video-range matrix or NV12 plane mapping; report what you see.
- Black frame → DRM content (expected) or the capture pointed at the wrong display.

---

## M4 — full live pipeline (the M4 gate)

Goal/gate: the iPad shows the **live** Windows second screen with usable latency; drag a window onto it.

### 1. Build

```
cmake --build build --config Debug --target td_stream
```

### 2. Run

Launch the iPad app (M3ContentView, listening on 2345). Then on the host:

```
build\tools\stream\Debug\td_stream.exe 2345
```

It brings up the virtual display and streams continuously (capture → NVENC → §5 → usbmux). The iPad's
`VideoSession` decodes every frame and renders the latest; it sends a KEYFRAME_REQUEST at startup, and
the host forces an IDR in response. Ctrl+C (or disconnecting the iPad) stops the host.

### 3. Verify (the gate)

- The iPad shows your Windows second display **live**. Drag a window onto the virtual display (in
  Windows Display Settings it's the 2360×1640 monitor) and watch it appear on the iPad.
- Latency should feel usable (tens of ms is expected and good — see §5 of this runbook for a low-tech
  measurement; sub-10 ms claims are marketing).

### 4. Troubleshooting

- iPad shows the first frame then freezes → frames are arriving but not decoding; check that subsequent
  VIDEO_FRAMEs are deltas referencing the IDR (the host logs frames sent).
- Stutter / growing latency → bitrate too high for USB-2; lower `target_bitrate_kbps` in `td_stream`
  (bitrate adaptation under load is M5).
- "no second display" → the virtual display driver (M1.1) or Apple Mobile Device Service isn't up.
- The host orchestration itself (continuous streaming, IDR-on-request, PING→PONG) is covered by the
  `core_test` live-loop test, which runs in CI/WSL with fakes — a host that passes that but fails here
  points at the real capture/encode/transport, not the session logic.

---

## M5 — pairing + encryption + recovery (the M5 gate)

The live stream now pairs (TOFU) and is AEAD-encrypted, and auto-recovers on cable kill/replug.

1. Build `td_stream` (it now links libsodium): `cmake -S . -B build -DNVENC_INCLUDE_DIR=… -DSODIUM_DIR=…`
   then `cmake --build build --config Debug --target td_stream`.
2. First run pairs (TOFU): the iPad learns + pins the host identity, the host pins the iPad. Run
   `td_stream 2345` with the iPad app open; it shows "Paired + encrypted".
3. **Gate — recovery:** while streaming, **unplug the USB cable**, wait, replug. The host loops back to
   "waiting for iPad" and re-pairs automatically; the iPad resumes. **Gate — refusal:** install the app
   on a *different* iPad (different identity) → the host logs `pairing refused (unknown/unpaired device)`
   and does not stream.
4. Identity keys live in Windows Credential Manager (host) / Keychain (iPad) — see `docs/SECURITY.md`.

---

## M6 — tray app, config, and the cold-start dry run (the project gate)

### Tray UI

Build `td_tray` (`cmake --build build --config Debug --target td_tray`) and run it. A tray icon appears;
right-click for **Start / Stop / Quit**. It starts streaming immediately and shows status in the tooltip
("waiting for iPad…", "streaming (paired)", "pairing refused/failed"). It reads
`%APPDATA%\TetherDisplay\config.txt` (a plain `key=value` file: `device_port`, `codec=hevc|h264`,
`target_bitrate_kbps`, `max_bitrate_kbps`, `fps`); values are clamped to safe ranges (the encoder never
exceeds the USB-2 budget). No secrets are in this file.

### Cold-start dry run (Definition of Done — §10)

From a clean machine, following ONLY this runbook, you should be able to:
1. Install Apple Mobile Device Support, the Parsec virtual display driver, and (optionally) add the
   2360×1640 custom resolution. (M1.1, M2)
2. Build the host (`td_tray` or the per-milestone sandboxes) with the Windows SDK, NVIDIA Video Codec
   SDK, and libsodium.
3. Build the iPad app on a Mac (free personal team), sideload + auto-refresh via AltStore. (M2, M3)
4. Plug in and trust the iPad; launch the iPad app and `td_tray` (or `td_stream`).
5. See the Windows second screen on the iPad at native resolution, 30+ fps; drag a window onto it.
6. Pull and replug the cable → it auto-recovers; confirm a different (unpaired) iPad is refused.

If every step above succeeds from a clean state, the project meets its Definition of Done.
