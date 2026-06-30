// HEVC hardware decode via VideoToolbox (M3). Takes one HEVC access unit (Annex-B, as NVENC emits with
// repeatSPSPPS=1), builds the format description from VPS/SPS/PPS, converts the VCL NALs to HVCC
// length-prefixed form, and decodes to a CVPixelBuffer (NV12). Implemented against the verified recipe
// in docs/ARCHITECTURE.md. No special entitlement needed.
//
// ⛔ Built on a Mac (the agent cannot compile Swift/VideoToolbox); verified on the iPad. Points marked
//    [VERIFY-ON-HW] need on-device confirmation.
import CoreMedia
import Foundation
import VideoToolbox

final class HEVCDecoder {
    /// Called with each decoded frame (on VideoToolbox's callback thread).
    var onFrame: ((CVPixelBuffer) -> Void)?

    private var formatDesc: CMFormatDescription?
    private var session: VTDecompressionSession?

    deinit { invalidate() }

    func invalidate() {
        if let s = session { VTDecompressionSessionInvalidate(s) }
        session = nil
        formatDesc = nil
    }

    static func hardwareDecodeSupported() -> Bool {
        VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)
    }

    // HEVC NAL types we care about (H.265 spec; not an Apple constant).
    private enum NAL { static let vps: UInt8 = 32, sps: UInt8 = 33, pps: UInt8 = 34 }
    private static func nalType(_ nal: Data) -> UInt8 { ((nal.first ?? 0) >> 1) & 0x3F }
    /// IRAP/keyframe slice types (BLA…CRA = 16…21).
    static func isKeyframeAccessUnit(_ accessUnit: Data) -> Bool {
        splitNALUnits(accessUnit).contains { let t = nalType($0); return t >= 16 && t <= 21 }
    }

    /// Decode one Annex-B access unit. Param sets (if present) refresh the format description.
    func decode(_ accessUnit: Data) {
        let nals = Self.splitNALUnits(accessUnit)
        var vps: Data?, sps: Data?, pps: Data?
        var vcl: [Data] = []
        for nal in nals {
            switch Self.nalType(nal) {
            case NAL.vps: vps = nal
            case NAL.sps: sps = nal
            case NAL.pps: pps = nal
            case let t where t <= 31: vcl.append(nal)  // slice NAL
            default: break
            }
        }
        if let v = vps, let s = sps, let p = pps { makeFormatDescription(v, s, p) }
        guard let fmt = formatDesc, !vcl.isEmpty else { return }
        if session == nil { createSession(fmt) }
        guard let session else { return }

        // Annex-B -> HVCC: 4-byte big-endian length prefix per VCL NAL (param sets are in `fmt`).
        var avcc = Data()
        for nal in vcl {
            var len = UInt32(nal.count).bigEndian
            withUnsafeBytes(of: &len) { avcc.append(contentsOf: $0) }
            avcc.append(nal)
        }
        guard let sample = makeSampleBuffer(avcc, fmt) else { return }
        var infoOut = VTDecodeInfoFlags()
        VTDecompressionSessionDecodeFrame(session, sampleBuffer: sample, flags: [],
                                          frameRefcon: nil, infoFlagsOut: &infoOut)
    }

    // MARK: - building blocks

    private func makeFormatDescription(_ vps: Data, _ sps: Data, _ pps: Data) {
        vps.withUnsafeBytes { v in sps.withUnsafeBytes { s in pps.withUnsafeBytes { p in
            let ptrs: [UnsafePointer<UInt8>] = [
                v.bindMemory(to: UInt8.self).baseAddress!,
                s.bindMemory(to: UInt8.self).baseAddress!,
                p.bindMemory(to: UInt8.self).baseAddress!]
            let sizes = [vps.count, sps.count, pps.count]
            var fmt: CMFormatDescription?
            let status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
                allocator: kCFAllocatorDefault, parameterSetCount: 3,
                parameterSetPointers: ptrs, parameterSetSizes: sizes,
                nalUnitHeaderLength: 4, extensions: nil, formatDescriptionOut: &fmt)
            if status == noErr {
                // Recreate the session if the parameter sets changed.
                if let fmt, let existing = formatDesc, !CMFormatDescriptionEqual(fmt, otherFormatDescription: existing) {
                    invalidate()
                }
                formatDesc = fmt
            }
        }}}
    }

    private func createSession(_ fmt: CMFormatDescription) {
        let attrs: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String:
                Int(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),  // NV12 video-range
            kCVPixelBufferMetalCompatibilityKey as String: true,
        ]
        var record = VTDecompressionOutputCallbackRecord(
            decompressionOutputCallback: Self.outputCallback,
            decompressionOutputRefCon: Unmanaged.passUnretained(self).toOpaque())
        var s: VTDecompressionSession?
        let status = VTDecompressionSessionCreate(
            allocator: kCFAllocatorDefault, formatDescription: fmt, decoderSpecification: nil,
            imageBufferAttributes: attrs as CFDictionary, outputCallback: &record,
            decompressionSessionOut: &s)
        if status == noErr { session = s }
    }

    private func makeSampleBuffer(_ avcc: Data, _ fmt: CMFormatDescription) -> CMSampleBuffer? {
        let n = avcc.count
        var block: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(
                allocator: kCFAllocatorDefault, memoryBlock: nil, blockLength: n,
                blockAllocator: kCFAllocatorDefault, customBlockSource: nil, offsetToData: 0,
                dataLength: n, flags: kCMBlockBufferAssureMemoryNowFlag, blockBufferOut: &block)
                == kCMBlockBufferNoErr, let block else { return nil }
        let copied = avcc.withUnsafeBytes {
            CMBlockBufferReplaceDataBytes(with: $0.baseAddress!, blockBuffer: block,
                                          offsetIntoDestination: 0, dataLength: n)
        }
        guard copied == kCMBlockBufferNoErr else { return nil }

        var sample: CMSampleBuffer?
        var sizes = [n]
        guard CMSampleBufferCreateReady(
                allocator: kCFAllocatorDefault, dataBuffer: block, formatDescription: fmt,
                sampleCount: 1, sampleTimingEntryCount: 0, sampleTimingArray: nil,
                sampleSizeEntryCount: 1, sampleSizeArray: &sizes, sampleBufferOut: &sample)
                == noErr else { return nil }
        return sample
    }

    // Capture-free C callback: recover `self` from the refcon and forward the pixel buffer.
    private static let outputCallback: VTDecompressionOutputCallback = {
        refcon, _, status, _, imageBuffer, _, _ in
        guard status == noErr, let refcon, let imageBuffer else { return }
        let decoder = Unmanaged<HEVCDecoder>.fromOpaque(refcon).takeUnretainedValue()
        decoder.onFrame?(imageBuffer)
    }

    // Split an Annex-B buffer into NAL payloads (start codes removed).
    static func splitNALUnits(_ data: Data) -> [Data] {
        let b = [UInt8](data)
        let n = b.count
        func startCodeLen(_ p: Int) -> Int {
            if p + 3 < n, b[p] == 0, b[p + 1] == 0, b[p + 2] == 0, b[p + 3] == 1 { return 4 }
            if p + 2 < n, b[p] == 0, b[p + 1] == 0, b[p + 2] == 1 { return 3 }
            return 0
        }
        var nals: [Data] = []
        var i = 0
        var nalStart = -1
        while i < n {
            let sc = startCodeLen(i)
            if sc > 0 {
                if nalStart >= 0, i > nalStart { nals.append(Data(b[nalStart..<i])) }
                i += sc
                nalStart = i
            } else {
                i += 1
            }
        }
        if nalStart >= 0, nalStart < n { nals.append(Data(b[nalStart..<n])) }
        return nals
    }
}
