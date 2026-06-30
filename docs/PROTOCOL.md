# TetherDisplay Wire Protocol — v1 (FREEZE-CANDIDATE)

> Single source of truth for the on-the-wire bytes. Both `windows-host` and `ios-client`
> implement exactly this and are validated against `protocol/test-vectors/`.
> **Status:** freeze-candidate. Frozen once host (M0) **and** iPad (M3) both pass the vectors.
> After freeze, any change requires a `PROTOCOL_VERSION` bump **and** human sign-off (R6).

## 0. Conventions

- All multi-byte integers are **little-endian**. (Chosen for parity with x86 host and ARM iPad,
  both little-endian; no byte-swapping on either side.)
- `uN` = unsigned N-bit integer. Strings are UTF-8, length-prefixed (never NUL-terminated).
- `PROTOCOL_VERSION = 1`.

## 1. Framing

Every message on the wire is a single frame:

```
+----------------+---------+-------+--------+-------------------+
| length  (u32)  | channel | type  | flags  | payload (length-3)|
|                |  (u8)   | (u8)  | (u8)   |                   |
+----------------+---------+-------+--------+-------------------+
   4 bytes          1         1       1        length-3 bytes
```

- **`length`** counts every byte **after** the length field itself, i.e.
  `length = 3 + len(payload)`. The full frame on the wire is `4 + length` bytes.
  (Decision: length **excludes** itself. Rationale: a reader reads 4 bytes, then reads
  exactly `length` more bytes to get the rest of the frame.)
- **`length` bounds:** `length >= 3` (3 = the channel+type+flags triplet, empty payload).
  `len(payload) <= MAX_PAYLOAD = 16 MiB (16777216)`. A frame whose `length-3` exceeds
  `MAX_PAYLOAD`, or whose `length < 3`, is rejected (`OversizedMessage` / `BadField`) — never allocated.
- **`channel`**: `0 = control`, `1 = video`. Any other value → `UnknownChannel`.
  The host MUST prioritize draining channel 0 over channel 1 so a video burst can't starve control.
- **`flags`**: reserved in v1. Senders MUST set `0`. Receivers MUST reject non-zero → `ReservedFlagsSet`.
  (Reserved so future versions can add per-frame semantics behind a version bump.)
- **`type`**: interpreted **within its channel** (see §2, §3). An unknown `(channel, type)`
  pair → `UnknownType`.

## 2. Control channel (channel = 0)

| type   | name              | direction      | payload |
|--------|-------------------|----------------|---------|
| `0x01` | HELLO             | both, on connect | §2.1 |
| `0x02` | WELCOME           | host → client  | §2.2 |
| `0x03` | ERROR             | both           | §2.3 |
| `0x10` | KEYFRAME_REQUEST  | client → host  | §2.4 |
| `0x11` | FORMAT_CHANGE     | host → client  | §2.2 body (re-uses WELCOME) |
| `0x20` | PING              | both           | §2.5 |
| `0x21` | PONG              | both           | §2.5 |
| `0x2F` | DISCONNECT        | both           | §2.6 |

### 2.1 HELLO (14 bytes) — capability advertisement
| field | type | notes |
|-------|------|-------|
| protocol_version | u16 | `1` |
| codec_mask | u8 | bit0=HEVC(`0x01`), bit1=H264(`0x02`) |
| max_width | u16 | |
| max_height | u16 | |
| max_bitrate_kbps | u32 | |
| max_fps | u16 | |
| feature_flags | u8 | reserved `0` in v1; bit0 reserved for "encryption-capable" (M5) |

### 2.2 WELCOME (14 bytes) — negotiated session format (also FORMAT_CHANGE body)
| field | type | notes |
|-------|------|-------|
| protocol_version | u16 | `1` |
| codec_id | u8 | HEVC=`1`, H264=`2` |
| width | u16 | |
| height | u16 | |
| bitrate_kbps | u32 | |
| fps | u16 | |
| session_flags | u8 | reserved `0` in v1 |

### 2.3 ERROR (>= 4 bytes) — typed failure
| field | type | notes |
|-------|------|-------|
| code | u16 | see codes below |
| reason_len | u16 | MUST equal the remaining payload byte count, else `BadField` |
| reason | u8 × reason_len | UTF-8 |

Error codes: `1`=VERSION_MISMATCH, `2`=UNSUPPORTED_CODEC, `3`=INTERNAL,
`4`=PROTOCOL_VIOLATION, `5`=UNAUTHORIZED (reserved for M5 pairing).

### 2.4 KEYFRAME_REQUEST (5 bytes) — client → host, recovery
| field | type | notes |
|-------|------|-------|
| reason | u8 | `0`=startup, `1`=packet_loss, `2`=decode_error |
| last_good_seq | u32 | last successfully decoded video seq, or `0` |

### 2.5 PING / PONG (8 bytes)
| field | type | notes |
|-------|------|-------|
| token | u64 | opaque; PONG echoes the PING's token verbatim |

### 2.6 DISCONNECT (>= 4 bytes)
Same layout as ERROR (§2.3). Codes: `0`=normal, `1`=going_away, `2`=error.

## 3. Video channel (channel = 1)

| type   | name        | direction     | payload |
|--------|-------------|---------------|---------|
| `0x01` | VIDEO_FRAME | host → client | §3.1 |

### 3.1 VIDEO_FRAME (15-byte header + bitstream)
| field | type | notes |
|-------|------|-------|
| codec_id | u8 | HEVC=`1`, H264=`2` |
| frame_type | u8 | `0`=delta, `1`=IDR/keyframe |
| vflags | u8 | reserved `0` in v1 |
| pts_us | u64 | presentation timestamp, microseconds |
| seq | u32 | monotonic per session, wraps at 2^32 |
| bitstream | u8 × (payload_len - 15) | the encoded HEVC/H264 access unit; never raw pixels |

## 4. Handshake & negotiation

1. On connect, **both** sides send HELLO advertising their capabilities.
2. The **host** computes the session format from the two HELLOs (pure function `negotiate`):
   - require `host.version == client.version` (both `1`), else send ERROR `VERSION_MISMATCH` and stop.
   - codec: HEVC if both masks include HEVC, else H264 if both include H264, else ERROR `UNSUPPORTED_CODEC`.
   - width/height/bitrate/fps = element-wise **min** of the two sides' maxima.
3. Host sends WELCOME with the chosen format; streaming begins on channel 1.
4. Mid-session the host may send FORMAT_CHANGE (same body as WELCOME) to adapt under the USB-2 budget.
5. Version incompatibility fails **loudly** with a typed ERROR — never guess or downgrade silently.

## 5. Liveness & teardown

- Either side may send PING; the peer MUST reply PONG echoing the token. (Timeout policy is an
  endpoint concern, not a wire concern; recommended: ping every 1 s, declare dead after 3 missed.)
- DISCONNECT signals intentional teardown. Absence of it (e.g. cable pull) is detected by the
  transport layer (socket close) — the protocol does not depend on a graceful DISCONNECT.

## 6. Reserved for future versions (do not implement in v1)

- Control types `0x30`–`0x3F` are reserved for M5 pairing/auth (TOFU key exchange) and the
  AEAD-encryption handshake. Adding them is a version bump, not a v1 change.
- `flags` (frame) bit0 and `feature_flags` bit0 are reserved as described above.

## 7. Error model

Decoders perform bounds-checked reads and never over-read. Defined errors:
`ShortBuffer`, `OversizedMessage`, `UnknownChannel`, `UnknownType`, `ReservedFlagsSet`,
`BadField`, `TrailingBytes` (fixed-size message had extra bytes), `UnsupportedVersion`,
`NeedMoreData` (incremental parser: a full frame is not yet buffered).
