# TetherDisplay — Security & Threat Model

## Assets
- The live contents of the user's screen (the video stream).
- The pairing keys: each device's long-term Ed25519 identity secret key.

## Design (M5)

**Transport security sits BELOW the §5 protocol.** The usbmux byte tunnel is wrapped by a
`SecureByteChannel` (host) / `SecureTransport` (iPad) that performs a pairing handshake and then
transparently AEAD-encrypts every byte. The §5 wire protocol (still v1, frozen) runs unchanged on top
of the encrypted channel, so neither side's framing code knows about crypto. (The pairing sub-protocol
has its own label `TetherDisplay-v2-pairing`; §5 itself was not version-bumped.)

### Pairing — trust on first use (TOFU), MITM-resistant, forward-secret
- Each device has a long-term **Ed25519** identity keypair (`crypto_sign`), generated once and stored
  in OS secure storage (host: Windows Credential Manager / DPAPI; iPad: Keychain,
  `AfterFirstUnlockThisDeviceOnly`). The Ed25519 **public key is the device's stable identity**.
- Per connection, both sides generate a fresh **ephemeral X25519** keypair (`crypto_kx`), exchange the
  ephemeral public keys, and each **signs a transcript** — `label || serverIdentity || clientIdentity
  || serverEphemeral || clientEphemeral` — with its identity secret key. Each verifies the peer's
  signature against the **pinned** identity (or learns + pins it on first pairing) **before** deriving
  any keys. Signing the whole transcript (not just one's own ephemeral) blocks unknown-key-share /
  reflection, and the fresh ephemerals give liveness/anti-replay.
- **Mutual pinning:** the host pins the iPad's identity and the iPad pins the host's. An unknown /
  changed identity is **refused** (`PeerRejected`) before any data flows.
- Session keys come from `crypto_kx` over the **ephemeral** keypairs (host = server, iPad = client), so
  keys are fresh each connection — **forward secrecy**.

### Channel encryption
- Two independent `crypto_secretstream_xchacha20poly1305` streams (one per direction), keyed by the kx
  rx/tx outputs (client_tx == server_rx). Each side sends its 24-byte header first, then every record is
  `push`/`pull` with the Poly1305 tag verified on receipt — **tampering/truncation/reorder is detected**
  and is terminal.

These properties are exercised by `windows-host/crypto` unit tests against real libsodium: paired
round-trips, tamper rejection, **unknown-device refusal**, forged-signature/MITM rejection, and
fresh-keys-on-reconnect.

## Threat model
**In scope & mitigated:**
- Another USB device impersonating the paired iPad → refused by mutual TOFU pinning.
- A malicious local process connecting to the host's listener → it cannot complete the handshake
  without the pinned identity's secret key.
- Tampering/replay on the tunnel → AEAD tag failure (tamper) + fresh per-session keys (replay).
- Secrets at rest → identity keys in Credential Manager (DPAPI) / Keychain; never in source or logs.
- No screen content in logs (R7); the only data path is host↔iPad over USB.

**Explicit non-goals (honest):**
- This is **not** "unbreakable." A privileged local attacker who already controls the host can see the
  screen regardless. The at-rest key protection is DPAPI per-user / Keychain device-bound — encryption
  at rest vs other users, **not** a defense against same-user malware on an already-compromised host.
- Physical attacks on an unlocked machine are out of scope.
- `ThisDeviceOnly` Keychain accessibility means the iPad key does not migrate/restore to a new device —
  the user re-pairs after device migration (correct for a device-bound identity).

## `[VERIFY-ON-HW]`
- The handshake + AEAD framing interop between the host (libsodium) and iPad (swift-sodium) must be
  confirmed on device — both implement the identical byte layout, but only a live pairing proves it.
