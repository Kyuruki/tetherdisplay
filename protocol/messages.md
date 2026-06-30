# Protocol message catalog (human-readable companion to docs/PROTOCOL.md)

`docs/PROTOCOL.md` is the authoritative byte-level spec. This file is a quick index of the
message set and the shared test vectors that pin each one to exact bytes.

## Frame
`[u32 length][u8 channel][u8 type][u8 flags][payload]` — little-endian; `length = 3 + payload`.

## Messages

| channel | type   | name             | golden vector |
|---------|--------|------------------|---------------|
| 0       | `0x01` | HELLO            | `test-vectors/hello.hex` |
| 0       | `0x02` | WELCOME          | `test-vectors/welcome.hex` |
| 0       | `0x03` | ERROR            | `test-vectors/error.hex` |
| 0       | `0x10` | KEYFRAME_REQUEST | `test-vectors/keyframe_request.hex` |
| 0       | `0x11` | FORMAT_CHANGE    | `test-vectors/format_change.hex` |
| 0       | `0x20` | PING             | `test-vectors/ping.hex` |
| 0       | `0x21` | PONG             | `test-vectors/pong.hex` |
| 0       | `0x2F` | DISCONNECT       | `test-vectors/disconnect.hex` |
| 1       | `0x01` | VIDEO_FRAME      | `test-vectors/video_frame_idr.hex` |

Both endpoints are correct **iff** they encode/decode every message to/from the bytes in
`test-vectors/`. The Windows host proves this in `windows-host/transport/protocol/tests`;
the iOS client will prove the same against the identical files at M3.
