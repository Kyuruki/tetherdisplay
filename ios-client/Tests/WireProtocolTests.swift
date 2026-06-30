// Swift §5 protocol tests — validate the iOS codec against the SAME protocol/test-vectors/ the C++ host
// uses, so both endpoints are correct iff they match the shared bytes (the §5 freeze condition).
//
// Setup on the Mac: add WireProtocol.swift + this file to a test target (or `@testable import` the app
// module that contains WireProtocol.swift). The vectors are located relative to this source file via
// #filePath, so no bundling is required when running tests from the repo checkout.
import XCTest

final class WireProtocolTests: XCTestCase {

    // MARK: helpers

    private func vectorsDir() -> URL {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()   // ios-client/Tests
            .deletingLastPathComponent()   // ios-client
            .deletingLastPathComponent()   // repo root
            .appendingPathComponent("protocol/test-vectors")
    }

    private func fromHex(_ s: String) -> Data {
        func nibble(_ c: Character) -> UInt8? {
            guard let a = c.hexDigitValue else { return nil }
            return UInt8(a)
        }
        var out = Data()
        var hi: UInt8?
        for c in s {
            guard let v = nibble(c) else { continue }  // skip whitespace/newlines
            if let h = hi { out.append((h << 4) | v); hi = nil } else { hi = v }
        }
        return out
    }

    private func loadVector(_ name: String) throws -> Data {
        let url = vectorsDir().appendingPathComponent(name)
        return fromHex(try String(contentsOf: url, encoding: .utf8))
    }

    private func decodeOne(_ bytes: Data) -> Result<Message, WireError> {
        var p = FrameParser()
        p.feed(bytes)
        switch p.next() {
        case .success(let raw?): return decodeMessage(raw)
        case .success(nil): return .failure(.shortBuffer)
        case .failure(let e): return .failure(e)
        }
    }

    private func expectRoundtrip(_ name: String, _ expected: Message) throws {
        let golden = try loadVector(name)
        switch decodeOne(golden) {
        case .success(let m): XCTAssertEqual(m, expected, "\(name) decoded fields differ")
        case .failure(let e): XCTFail("\(name) decode failed: \(e)")
        }
        XCTAssertEqual(encode(expected), golden, "\(name) re-encode does not match golden bytes")
    }

    // MARK: golden vectors (same files the C++ tests use)

    func testPing() throws { try expectRoundtrip("ping.hex", .ping(Ping(token: 0x0102_0304_0506_0708))) }
    func testPong() throws { try expectRoundtrip("pong.hex", .pong(Pong(token: 0x0102_0304_0506_0708))) }

    func testKeyframeRequest() throws {
        try expectRoundtrip("keyframe_request.hex",
                            .keyframeRequest(KeyframeRequest(reason: 1, lastGoodSeq: 255)))
    }

    func testHello() throws {
        try expectRoundtrip("hello.hex", .hello(Hello(
            version: 1, codecMask: Wire.maskHEVC | Wire.maskH264,
            maxWidth: 2360, maxHeight: 1640, maxBitrateKbps: 80000, maxFps: 60, featureFlags: 0)))
    }

    func testWelcome() throws {
        try expectRoundtrip("welcome.hex", .welcome(Welcome(
            version: 1, codecId: Wire.codecHEVC, width: 2360, height: 1640,
            bitrateKbps: 60000, fps: 60, flags: 0)))
    }

    func testFormatChange() throws {
        try expectRoundtrip("format_change.hex", .formatChange(FormatChange(format: Welcome(
            version: 1, codecId: Wire.codecH264, width: 1180, height: 820,
            bitrateKbps: 30000, fps: 30, flags: 0))))
    }

    func testError() throws {
        try expectRoundtrip("error.hex", .error(ErrorMsg(code: Wire.errVersionMismatch, reason: "bad version")))
    }

    func testDisconnect() throws {
        try expectRoundtrip("disconnect.hex", .disconnect(Disconnect(code: 0, reason: "bye")))
    }

    func testVideoFrameIDR() throws {
        try expectRoundtrip("video_frame_idr.hex", .videoFrame(VideoFrame(
            codecId: Wire.codecHEVC, frameType: Wire.frameIDR, vflags: 0,
            ptsUs: 1_000_000, seq: 1, bitstream: Data([0x00, 0x00, 0x00, 0x01, 0x67, 0xAA, 0xBB]))))
    }

    // MARK: negative cases (must reject)

    func testVersionMismatchRejected() {
        let v2 = encode(.hello(Hello(version: 2, codecMask: Wire.maskHEVC,
                                     maxWidth: 2360, maxHeight: 1640, maxBitrateKbps: 80000, maxFps: 60)))
        XCTAssertEqual(decodeOne(v2), .failure(.unsupportedVersion))
    }

    func testReservedFlagsRejected() {
        var bytes = encode(.ping(Ping(token: 1)))
        bytes[6] = 0x01  // set a reserved frame flag
        XCTAssertEqual(decodeOne(bytes), .failure(.reservedFlagsSet))
    }

    func testTrailingBytesRejected() {
        // PING is 8 payload bytes; 9 must be rejected.
        let bytes = encodeFrame(.control, Wire.tPing, Data(repeating: 0, count: 9))
        XCTAssertEqual(decodeOne(bytes), .failure(.trailingBytes))
    }

    func testEmptyBitstreamVideoFrameRoundTrips() {
        let f = VideoFrame(codecId: Wire.codecH264, frameType: Wire.frameDelta, vflags: 0,
                           ptsUs: 42, seq: 7, bitstream: Data())
        XCTAssertEqual(decodeOne(encode(.videoFrame(f))), .success(.videoFrame(f)))
    }
}
