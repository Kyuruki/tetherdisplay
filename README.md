# TetherDisplay

Private-build software to use an **iPad (10th gen) as a USB-C second monitor for a Windows
laptop** — video out only. There is no native "display input" on an iPad; this is a low-latency
**compressed-video streaming pipeline that runs over the USB cable**:

```
Windows host: virtual display (IddCx) -> capture (WGC) -> NVENC encode (HEVC/H264, low-latency)
   -> usbmux -> Apple Mobile Device Service ==USB-C==> PeerTalk -> VideoToolbox decode
   -> Metal render -> iPad screen
```

Local-only: **no Wi-Fi, no LAN, no cloud, no telemetry — ever.** The only data path is host↔iPad
over the cable.

## Status — all milestones M0–M6 code-complete

| Milestone | What | Verification |
|---|---|---|
| **M0** | §5 wire protocol + offline loopback | 29 protocol tests + loopback (WSL) |
| **M1.1** | Virtual display (Parsec VDD behind `IVirtualDisplay`) | 8 vdisplay tests; Win32 vs verified IOCTLs |
| **M1.2** | WGC capture on the NVIDIA adapter + cross-adapter bridge | 7 capture tests; verified WGC/D3D11 docs |
| **M1.3** | NVENC low-latency HEVC encode-to-file | 8 encode tests; line-cited NVENC recipe |
| **M2** | Clean-room usbmux transport (host→device bytes) | 13 usbmux tests incl. full mock-daemon flow |
| **M3** | iPad VideoToolbox decode + Metal render (§5 frozen) | Swift §5 codec vs the SAME vectors |
| **M4** | Full live pipeline (continuous streaming session) | 11 core tests incl. off-device live-loop |
| **M5** | TOFU pairing + AEAD encryption, reconnect, adaptation | 10 crypto tests vs real libsodium |
| **M6** | Tray UI, config, full docs, cold-start runbook | 17 core tests (config); docs complete |

**93 portable unit tests pass in WSL.** Everything portable (all protocol/codec/policy/crypto logic) is
compiled and tested by the build agent; all Win32/WinRT/D3D11/NVENC/Swift code is written against
verified, citation-backed docs (`docs/ARCHITECTURE.md`, `docs/SECURITY.md`) and is **built on Windows /
Mac and verified on the hardware** by the operator — the agent has no access to the GPU, driver, iPad,
or Mac. Follow [`docs/RUNBOOK.md`](docs/RUNBOOK.md) for the cold-start bring-up.

## Repository layout

```
docs/         PROTOCOL.md (frozen contract), THIRD_PARTY.md, + RUNBOOK/SECURITY/PRIVACY (later)
protocol/     language-neutral message catalog + shared test-vectors/
windows-host/ C++ host service (transport/protocol implemented; capture/encode/usbmux later)
ios-client/   Swift/Xcode iPad app (added at M2/M3; built on a Mac, sideloaded via AltStore)
tools/        loopback simulator (latency harness + sandboxes later)
```

## Building & testing the protocol module

**Windows / canonical (CMake):**
```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

**Linux / WSL (offline, no CMake) — for developing the portable protocol logic:**
```
cd windows-host/transport/protocol
make test        # GoogleTest unit + golden-vector suite
make loopback    # M0 in-process end-to-end gate
```

## License

Not yet chosen. The project **may be open-sourced later**, so all dependencies are kept
permissive (MIT/BSD/Apache); see [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md). Copyleft deps are
rejected or flagged.
