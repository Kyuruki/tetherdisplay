# Third-Party Dependencies & Licenses

Every external dependency or vendored component is recorded here with its license, per the
project's licensing rule (prefer MIT/BSD/Apache-2.0; **copyleft GPL/LGPL → STOP and flag to the
human** before use). `[UNVERIFIED]` marks a license I have not yet confirmed from the source.

## In use now (M0)

| Component | Version | Used for | Scope | License | Notes |
|-----------|---------|----------|-------|---------|-------|
| GoogleTest | 1.14.0 | Unit/vector tests | **test only** — not linked into any shipped binary | BSD-3-Clause | Permissive. On Ubuntu built from `/usr/src/googletest`; on Windows via `find_package`/FetchContent. |
| CMake | ≥3.20 | Build system | dev tool | BSD-3-Clause | Permissive; not distributed. |

No runtime third-party code is linked into the protocol module — it is pure standard C++20.

## Planned, NOT yet adopted (verify license before use — R6 stop-and-ask)

| Component | Planned use | Milestone | Expected license | Action required |
|-----------|-------------|-----------|------------------|-----------------|
| libsodium | AEAD channel + key exchange | M5 | ISC (permissive) | Confirm version + license at adoption; record here. |
| PeerTalk | iOS USB transport | M2/M3 | MIT (historically) | **[UNVERIFIED]** — confirm from its repo before vendoring. |
| Parsec Virtual Display Driver | IddCx virtual monitor (Option A) | M1 | **[UNVERIFIED]** | Confirm current license + IOCTL control API before adopting. |
| VirtualDrivers/Virtual-Display-Driver | IddCx virtual monitor (Option A alt) | M1 | **[UNVERIFIED]** | Confirm current license before adopting; prefer the more permissive of the two. |
| Microsoft IddCx sample | IddCx virtual monitor (Option B) | M1 (fallback) | MIT (Windows-driver-samples) | Self-sign for private use; cleanest license for a future OSS release. |

## Explicitly REJECTED

| Component | Reason |
|-----------|--------|
| libusbmuxd / libimobiledevice | **GPLv3** — would contaminate a future open-source release. The host speaks the usbmux socket protocol directly instead (clean-room, see §6.4 of the master prompt). |
