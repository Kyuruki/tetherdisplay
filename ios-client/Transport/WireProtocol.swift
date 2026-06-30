// TetherDisplay §5 wire protocol — Swift implementation (M0/M3). Mirrors windows-host/transport/
// protocol/src/wire.cpp byte-for-byte and is validated against the SAME protocol/test-vectors/. Pure
// Foundation, no Apple media frameworks — so WireProtocolTests can run it on the Mac to satisfy the
// §5 freeze condition (both endpoints match the vectors).
import Foundation

enum Channel: UInt8 { case control = 0, video = 1 }

enum WireError: Error, Equatable {
    case shortBuffer, oversizedMessage, unknownChannel, unknownType
    case reservedFlagsSet, badField, trailingBytes, unsupportedVersion
}

enum Wire {
    static let protocolVersion: UInt16 = 1
    static let maxPayload = 16 * 1024 * 1024

    // Control-channel types.
    static let tHello: UInt8 = 0x01, tWelcome: UInt8 = 0x02, tError: UInt8 = 0x03
    static let tKeyframeRequest: UInt8 = 0x10, tFormatChange: UInt8 = 0x11
    static let tPing: UInt8 = 0x20, tPong: UInt8 = 0x21, tDisconnect: UInt8 = 0x2F
    // Video-channel type.
    static let tVideoFrame: UInt8 = 0x01

    static let codecHEVC: UInt8 = 1, codecH264: UInt8 = 2
    static let maskHEVC: UInt8 = 0x01, maskH264: UInt8 = 0x02
    static let frameDelta: UInt8 = 0, frameIDR: UInt8 = 1

    static let errVersionMismatch: UInt16 = 1, errUnsupportedCodec: UInt16 = 2
    static let errInternal: UInt16 = 3, errProtocolViolation: UInt16 = 4, errUnauthorized: UInt16 = 5
}

// MARK: - Messages

struct Hello: Equatable {
    var version: UInt16 = Wire.protocolVersion
    var codecMask: UInt8 = 0
    var maxWidth: UInt16 = 0, maxHeight: UInt16 = 0
    var maxBitrateKbps: UInt32 = 0
    var maxFps: UInt16 = 0
    var featureFlags: UInt8 = 0
}

struct Welcome: Equatable {
    var version: UInt16 = Wire.protocolVersion
    var codecId: UInt8 = 0
    var width: UInt16 = 0, height: UInt16 = 0
    var bitrateKbps: UInt32 = 0
    var fps: UInt16 = 0
    var flags: UInt8 = 0
}

struct ErrorMsg: Equatable { var code: UInt16 = 0; var reason: String = "" }
struct KeyframeRequest: Equatable { var reason: UInt8 = 0; var lastGoodSeq: UInt32 = 0 }
struct FormatChange: Equatable { var format: Welcome }
struct Ping: Equatable { var token: UInt64 = 0 }
struct Pong: Equatable { var token: UInt64 = 0 }
struct Disconnect: Equatable { var code: UInt16 = 0; var reason: String = "" }
struct VideoFrame: Equatable {
    var codecId: UInt8 = 0
    var frameType: UInt8 = 0
    var vflags: UInt8 = 0
    var ptsUs: UInt64 = 0
    var seq: UInt32 = 0
    var bitstream: Data = Data()
}

enum Message: Equatable {
    case hello(Hello), welcome(Welcome), error(ErrorMsg), keyframeRequest(KeyframeRequest)
    case formatChange(FormatChange), ping(Ping), pong(Pong), disconnect(Disconnect), videoFrame(VideoFrame)
}

struct RawFrame { var channel: Channel; var type: UInt8; var flags: UInt8; var payload: Data }

// MARK: - Byte cursors (little-endian)

private struct ByteWriter {
    var data = Data()
    mutating func u8(_ v: UInt8) { data.append(v) }
    mutating func u16(_ v: UInt16) { for i in 0..<2 { data.append(UInt8((v >> (8 * i)) & 0xFF)) } }
    mutating func u32(_ v: UInt32) { for i in 0..<4 { data.append(UInt8((v >> (8 * UInt32(i))) & 0xFF)) } }
    mutating func u64(_ v: UInt64) { for i in 0..<8 { data.append(UInt8((v >> (8 * UInt64(i))) & 0xFF)) } }
    mutating func bytes(_ b: Data) { data.append(b) }
}

private struct ByteReader {
    private let d: [UInt8]
    private(set) var pos = 0
    private(set) var error = false
    init(_ data: Data) { d = [UInt8](data) }

    private mutating func ensure(_ n: Int) -> Bool {
        if error { return false }
        if d.count - pos < n { error = true; return false }
        return true
    }
    mutating func u8() -> UInt8 { guard ensure(1) else { return 0 }; defer { pos += 1 }; return d[pos] }
    mutating func u16() -> UInt16 {
        guard ensure(2) else { return 0 }
        let v = UInt16(d[pos]) | (UInt16(d[pos + 1]) << 8); pos += 2; return v
    }
    mutating func u32() -> UInt32 {
        guard ensure(4) else { return 0 }
        var v: UInt32 = 0; for i in 0..<4 { v |= UInt32(d[pos + i]) << (8 * UInt32(i)) }; pos += 4; return v
    }
    mutating func u64() -> UInt64 {
        guard ensure(8) else { return 0 }
        var v: UInt64 = 0; for i in 0..<8 { v |= UInt64(d[pos + i]) << (8 * UInt64(i)) }; pos += 8; return v
    }
    mutating func take(_ n: Int) -> Data {
        guard ensure(n) else { return Data() }
        defer { pos += n }
        return Data(d[pos..<pos + n])
    }
    var remaining: Int { error ? 0 : d.count - pos }
}

// MARK: - Encoding

func encodeFrame(_ channel: Channel, _ type: UInt8, _ payload: Data) -> Data {
    precondition(payload.count <= Wire.maxPayload)
    var w = ByteWriter()
    w.u32(UInt32(3 + payload.count))  // length excludes itself
    w.u8(channel.rawValue); w.u8(type); w.u8(0)  // flags reserved = 0
    w.bytes(payload)
    return w.data
}

private func putWelcome(_ w: inout ByteWriter, _ m: Welcome) {
    w.u16(m.version); w.u8(m.codecId); w.u16(m.width); w.u16(m.height)
    w.u32(m.bitrateKbps); w.u16(m.fps); w.u8(m.flags)
}

private func putReason(_ w: inout ByteWriter, _ code: UInt16, _ reason: String) {
    let utf8 = Array(reason.utf8)
    let n = min(utf8.count, 0xFFFF)  // clamp so reason_len always equals the bytes written
    w.u16(code); w.u16(UInt16(n)); w.bytes(Data(utf8.prefix(n)))
}

func encode(_ msg: Message) -> Data {
    var w = ByteWriter()
    let channel: Channel
    let type: UInt8
    switch msg {
    case .hello(let m):
        channel = .control; type = Wire.tHello
        w.u16(m.version); w.u8(m.codecMask); w.u16(m.maxWidth); w.u16(m.maxHeight)
        w.u32(m.maxBitrateKbps); w.u16(m.maxFps); w.u8(m.featureFlags)
    case .welcome(let m): channel = .control; type = Wire.tWelcome; putWelcome(&w, m)
    case .formatChange(let m): channel = .control; type = Wire.tFormatChange; putWelcome(&w, m.format)
    case .error(let m): channel = .control; type = Wire.tError; putReason(&w, m.code, m.reason)
    case .disconnect(let m): channel = .control; type = Wire.tDisconnect; putReason(&w, m.code, m.reason)
    case .keyframeRequest(let m):
        channel = .control; type = Wire.tKeyframeRequest; w.u8(m.reason); w.u32(m.lastGoodSeq)
    case .ping(let m): channel = .control; type = Wire.tPing; w.u64(m.token)
    case .pong(let m): channel = .control; type = Wire.tPong; w.u64(m.token)
    case .videoFrame(let m):
        channel = .video; type = Wire.tVideoFrame
        w.u8(m.codecId); w.u8(m.frameType); w.u8(m.vflags); w.u64(m.ptsUs); w.u32(m.seq); w.bytes(m.bitstream)
    }
    return encodeFrame(channel, type, w.data)
}

// MARK: - Decoding

func decodeMessage(_ frame: RawFrame) -> Result<Message, WireError> {
    if frame.flags != 0 { return .failure(.reservedFlagsSet) }
    var r = ByteReader(frame.payload)

    func finish(_ m: Message) -> Result<Message, WireError> {
        if r.error { return .failure(.shortBuffer) }
        if r.remaining != 0 { return .failure(.trailingBytes) }
        return .success(m)
    }
    func readWelcome() -> Welcome {
        var m = Welcome()
        m.version = r.u16(); m.codecId = r.u8(); m.width = r.u16(); m.height = r.u16()
        m.bitrateKbps = r.u32(); m.fps = r.u16(); m.flags = r.u8()
        return m
    }

    switch frame.channel {
    case .control:
        switch frame.type {
        case Wire.tHello:
            var m = Hello()
            m.version = r.u16(); m.codecMask = r.u8(); m.maxWidth = r.u16(); m.maxHeight = r.u16()
            m.maxBitrateKbps = r.u32(); m.maxFps = r.u16(); m.featureFlags = r.u8()
            if r.error { return .failure(.shortBuffer) }
            if m.version != Wire.protocolVersion { return .failure(.unsupportedVersion) }
            return finish(.hello(m))
        case Wire.tWelcome:
            let m = readWelcome()
            if r.error { return .failure(.shortBuffer) }
            if m.version != Wire.protocolVersion { return .failure(.unsupportedVersion) }
            return finish(.welcome(m))
        case Wire.tFormatChange:
            let m = readWelcome()
            if r.error { return .failure(.shortBuffer) }
            if m.version != Wire.protocolVersion { return .failure(.unsupportedVersion) }
            return finish(.formatChange(FormatChange(format: m)))
        case Wire.tError, Wire.tDisconnect:
            let code = r.u16(); let reasonLen = Int(r.u16())
            if r.error { return .failure(.shortBuffer) }
            if r.remaining != reasonLen { return .failure(.badField) }
            let reason = String(decoding: r.take(reasonLen), as: UTF8.self)
            return frame.type == Wire.tError
                ? finish(.error(ErrorMsg(code: code, reason: reason)))
                : finish(.disconnect(Disconnect(code: code, reason: reason)))
        case Wire.tKeyframeRequest:
            var m = KeyframeRequest(); m.reason = r.u8(); m.lastGoodSeq = r.u32()
            if r.error { return .failure(.shortBuffer) }
            return finish(.keyframeRequest(m))
        case Wire.tPing:
            let m = Ping(token: r.u64()); if r.error { return .failure(.shortBuffer) }; return finish(.ping(m))
        case Wire.tPong:
            let m = Pong(token: r.u64()); if r.error { return .failure(.shortBuffer) }; return finish(.pong(m))
        default:
            return .failure(.unknownType)
        }
    case .video:
        if frame.type != Wire.tVideoFrame { return .failure(.unknownType) }
        var m = VideoFrame()
        m.codecId = r.u8(); m.frameType = r.u8(); m.vflags = r.u8(); m.ptsUs = r.u64(); m.seq = r.u32()
        if r.error { return .failure(.shortBuffer) }
        m.bitstream = r.take(r.remaining)  // rest of payload; no trailing-bytes check
        return .success(.videoFrame(m))
    }
}

// MARK: - Incremental frame parser

struct FrameParser {
    private var buf = [UInt8]()
    private var readPos = 0

    mutating func feed(_ data: Data) {
        if readPos > 0 && readPos == buf.count {
            buf.removeAll(keepingCapacity: true); readPos = 0
        } else if readPos > 64 * 1024 {
            buf.removeFirst(readPos); readPos = 0
        }
        buf.append(contentsOf: data)
    }

    /// Returns the next complete frame, `.success(nil)` if more bytes are needed, or a hard error
    /// (terminal: the wire has no resync marker, so the caller must tear down on a hard error).
    mutating func next() -> Result<RawFrame?, WireError> {
        let avail = buf.count - readPos
        if avail < 4 { return .success(nil) }
        var length: UInt32 = 0
        for i in 0..<4 { length |= UInt32(buf[readPos + i]) << (8 * UInt32(i)) }
        if length < 3 { return .failure(.badField) }
        if Int(length) - 3 > Wire.maxPayload { return .failure(.oversizedMessage) }
        if avail < 4 + Int(length) { return .success(nil) }

        let channelByte = buf[readPos + 4]
        guard let channel = Channel(rawValue: channelByte) else { return .failure(.unknownChannel) }
        let type = buf[readPos + 5]
        let flags = buf[readPos + 6]
        let payloadLen = Int(length) - 3
        let payload = Data(buf[(readPos + 7)..<(readPos + 7 + payloadLen)])
        readPos += 4 + Int(length)
        return .success(RawFrame(channel: channel, type: type, flags: flags, payload: payload))
    }
}
