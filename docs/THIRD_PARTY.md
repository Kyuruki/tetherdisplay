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
| nomi-san/parsec-vdd (Option A1) | IddCx virtual monitor — drive Parsec's signed VDD | M1 | Wrapper **MIT** (verified 2026-06-29); **driver binary is Parsec PROPRIETARY** (redistributed by Parsec only, pre-signed via SignPath) | **✅ SELECTED — R6-approved 2026-06-29.** Cleanest control: open driver handle → IOCTL add/remove (returns a display index), periodic keep-alive ping, change modes via Win32 Display API. Private-use OK; the proprietary binary is a blocker for **bundling** in a future OSS release — keep behind `IVirtualDisplay`, evaluate Option B before any OSS release. |
| VirtualDrivers/Virtual-Display-Driver (Option A2) | IddCx virtual monitor — fully open driver | M1 | **MIT** (verified 2026-06-29, Windows repo; full driver source; signed via SignPath) | Control via `vdd_settings.xml` + GUI (VDC) — less programmatic than IOCTL. ⚠️ The **Linux** variant is GPLv3/commercial — do not confuse; re-confirm the chosen release tag's LICENSE at adoption. |
| Microsoft IddCx sample (Option B) | IddCx virtual monitor — build + self-sign | M1 (OSS-release target) | **MS-PL** (Microsoft Public License — permissive, OSI-approved; **not** MIT) (verified 2026-06-29) | Self-contained + permissive → best OSS-later story; requires self-signing (test cert + trust on the operator's machine). |

## Explicitly REJECTED

| Component | Reason |
|-----------|--------|
| libusbmuxd / libimobiledevice | **GPLv3** — would contaminate a future open-source release. The host speaks the usbmux socket protocol directly instead (clean-room, see §6.4 of the master prompt). |
