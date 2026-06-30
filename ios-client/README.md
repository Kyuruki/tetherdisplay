# ios-client

The iPad app. Built on a borrowed Mac with Xcode (the agent cannot build/sign iOS), installed via
**free sideloading** + AltStore/SideStore refresh — so only free-personal-team-compatible entitlements
(no push, no associated domains, no app groups needing provisioning).

## Status

- **`M2Gate/GateListener.swift`** — the M2 receipt gate: a trivial SwiftUI app that listens for the
  usbmux-tunneled connection from the host and shows the received byte count + FNV-1a checksum. Used to
  prove bytes flow host→iPad over USB (M2's gate). It uses Apple's built-in **Network.framework**
  (`NWListener`) — no third-party dependency. (The full client's transport uses **PeerTalk** per the
  master prompt §7.2; that lands with M3's decode/render work.)

Later milestones add: `Transport/` (PeerTalk + the §5 protocol), `Decode/` (VideoToolbox), `Render/`
(Metal), `Session/`, `UI/`, `Tests/`.

## Building the M2 gate on a Mac

1. New Xcode project → iOS App → SwiftUI. Replace the generated `ContentView.swift` with
   `M2Gate/GateListener.swift` (it defines both `GateListener` and `ContentView`).
2. Signing & Capabilities → select your **free personal team**; set a unique bundle id
   (e.g. `com.<you>.tetherdisplay.m2gate`). No capabilities are required.
3. If iOS prompts for local network access, add `NSLocalNetworkUsageDescription` to Info.plist
   (**[VERIFY]** — the usbmux tunnel arrives as a localhost connection, which historically does not
   trigger the prompt).
4. Build to the connected iPad, or Product → Archive → export the `.ipa` for AltStore.

See `docs/RUNBOOK.md` (M2) for the host side and the end-to-end gate steps, and for the AltStore
sideload + 7-day refresh flow run from the Windows host.
