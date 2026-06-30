# TetherDisplay — Known Limitations

These are inherent to this class of software or to the chosen constraints. They are **not bugs** — do
not try to "fix" them; document and live with them.

## Video / display
- **DRM-protected content renders black.** Netflix, some DRM video, and other output-protected content
  may appear black on the virtual/extended display due to OS output protection. This is inherent to
  capturing an extended desktop; it is not a defect.
- **USB 2.0 caps the quality ceiling.** The iPad 10th-gen's USB-C port is USB 2.0 (480 Mbps line rate;
  far less usable after framing/overhead). A clean HEVC stream at native 2360×1640 fits comfortably
  (target ~40–60 Mbps, hard cap ~80 Mbps), but **raw/uncompressed frames are impossible** and are never
  sent. Under load the host adapts bitrate downward (M5) and may fall back from 60→30 fps.
- **Latency is tens of milliseconds, and that's good.** Capture → encode → USB → decode → render is a
  real pipeline; expect ~tens of ms. Anyone advertising sub-10 ms screen streaming is marketing.
- **Hybrid-GPU copy cost.** On the XPS the desktop may be composited on the iGPU while NVENC is on the
  RTX 4080; frames cross PCIe each frame (handled by capturing on the NVIDIA adapter — §6.4). This adds
  a small, unavoidable copy to the latency budget.

## Platform / scope (v1)
- **Video out only.** No touch, no Pencil, no audio, no input back to Windows, no clipboard, no file
  transfer, no multi-monitor-on-iPad, no wireless, no Mac host.
- **Non-standard resolution.** Native 2360×1640 must be registered as a custom resolution for the
  virtual display driver (RUNBOOK M1.1); otherwise the nearest preset is used and the iPad scales.

## Distribution / install
- **Free sideloading expires every 7 days.** The iPad app is installed via a free Apple personal team;
  the provisioning profile expires after 7 days and AltStore/SideStore must re-sign it. Keep AltServer
  running on the host. **Any iOS code change requires rebuilding the `.ipa` on a Mac** — AltStore only
  refreshes the existing signature.
- **Pairing is device-bound.** The iPad's identity key is stored `ThisDeviceOnly` in the Keychain and
  does not migrate/restore to a new device — re-pair after a device migration or app reinstall.

## Security (honest non-goals)
- Not "unbreakable": a privileged local attacker who already controls the host can see the screen
  anyway; at-rest key protection is DPAPI/Keychain, i.e. vs other users, not vs same-user malware. See
  `docs/SECURITY.md`.
