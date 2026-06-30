# TetherDisplay RUNBOOK (operator steps the agent cannot run)

Copy-pasteable, ordered steps the human performs on the hardware. Grows one milestone at a time.
Today it covers **M1.1 — virtual display bring-up**. (M0 needs no operator steps; build/test it with
`windows-host/transport/protocol/Makefile` on WSL or via CMake on Windows.)

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
