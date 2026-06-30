# TetherDisplay — Privacy Policy

This policy describes a **private, local-only build**. Adjust the language if the project is ever
distributed or hosted.

- TetherDisplay collects **no** personal data, sends **nothing** off your devices, and uses **no**
  servers, analytics, telemetry, crash reporting, or update checks. There is no outbound network of any
  kind (enforced as a hard invariant — see R7; any network call would be a bug).
- The **only** data flow is screen frames from the Windows host to the **paired** iPad over the local
  **USB** cable, end-to-end encrypted (see `docs/SECURITY.md`).
- No screen content is written to disk or logs. The host records nothing; the iPad displays frames and
  discards them.
- The only data stored at rest is local configuration and the device pairing keys, held in OS secure
  storage (Windows Credential Manager / iOS Keychain) — never transmitted.
- DRM-protected content (e.g. Netflix) may render black on the virtual display due to output
  protection; that is inherent and is documented in `docs/LIMITATIONS.md`, not a data path.

If you build, run, and pair the software yourself on your own two devices, no data leaves them.
