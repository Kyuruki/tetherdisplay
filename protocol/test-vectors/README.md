# Shared protocol test vectors

Each `*.hex` file is one complete wire frame as an ASCII-hex string (whitespace ignored).
These are the **cross-language contract**: every endpoint must decode them to the documented
fields and re-encode the fields back to the exact same bytes.

| file | message | decoded fields |
|------|---------|----------------|
| `ping.hex` | PING | token = 0x0102030405060708 |
| `pong.hex` | PONG | token = 0x0102030405060708 |
| `keyframe_request.hex` | KEYFRAME_REQUEST | reason = 1 (packet_loss), last_good_seq = 255 |
| `hello.hex` | HELLO | v1, codec_mask = HEVC\|H264, 2360×1640, 80000 kbps, 60 fps, feature_flags = 0 |
| `welcome.hex` | WELCOME | v1, codec = HEVC, 2360×1640, 60000 kbps, 60 fps, flags = 0 |
| `format_change.hex` | FORMAT_CHANGE | v1, codec = H264, 1180×820, 30000 kbps, 30 fps, flags = 0 |
| `error.hex` | ERROR | code = 1 (VERSION_MISMATCH), reason = "bad version" |
| `disconnect.hex` | DISCONNECT | code = 0 (normal), reason = "bye" |
| `video_frame_idr.hex` | VIDEO_FRAME | HEVC, IDR, pts_us = 1000000, seq = 1, bitstream = 00 00 00 01 67 AA BB |

Validated by `windows-host/transport/protocol/tests/wire_test.cpp` (the `Vectors.*` cases).
The iOS client adds the matching XCTest cases at M3.
