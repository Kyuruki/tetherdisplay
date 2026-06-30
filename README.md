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

## Status

**Milestone M0 — protocol + offline loopback: complete & passing.**
- Frozen-candidate wire protocol: [`docs/PROTOCOL.md`](docs/PROTOCOL.md)
- Portable C++20 codec + tests: `windows-host/transport/protocol/`
- Shared byte-level vectors: `protocol/test-vectors/`
- In-process loopback gate: `tools/loopback/`

Next: **M1** (virtual display + capture + NVENC encode-to-file). See the milestone list in the
master prompt and `docs/` (to be expanded).

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

## What this build can and cannot verify itself

This codebase is developed by an agent with **no access to the target hardware**. It produces the
source and runbooks; a human builds/signs the iOS app, installs the display driver, plugs in the
iPad, and measures real latency. M0 is fully verifiable in software (pure protocol logic), which
is why it is complete now; later milestones gate on human, on-device steps.

## License

Not yet chosen. The project **may be open-sourced later**, so all dependencies are kept
permissive (MIT/BSD/Apache); see [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md). Copyleft deps are
rejected or flagged.
