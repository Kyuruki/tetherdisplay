// Video session (M3): accepts the usbmux-tunneled connection, parses the §5 stream, and drives the
// HEVC decoder + Metal renderer. For the M3 gate the host pushes one keyframe; this shows it on screen.
// Sends HELLO + an initial KEYFRAME_REQUEST, and replies PONG to PING.
//
// ⛔ Built on a Mac; verified on the iPad. Uses Network.framework for the M2/M3 receipt gate; the full
//    client transport is PeerTalk per §7.2.
import CoreVideo
import Network
import SwiftUI
import UIKit

@MainActor
final class VideoSession: ObservableObject {
    @Published var status = "Starting…"
    @Published var framesDecoded = 0

    private let port: UInt16
    private var listener: NWListener?
    private var connection: NWConnection?
    private var parser = FrameParser()
    private let decoder = HEVCDecoder()
    weak var renderer: MetalVideoRenderer?

    init(port: UInt16 = 2345) {
        self.port = port
        decoder.onFrame = { [weak self] pb in
            Task { @MainActor in
                guard let self else { return }
                self.renderer?.setPixelBuffer(pb)
                self.framesDecoded += 1
                self.status = "Rendering \(CVPixelBufferGetWidth(pb))×\(CVPixelBufferGetHeight(pb))"
            }
        }
    }

    func start() {
        guard HEVCDecoder.hardwareDecodeSupported() else { status = "HEVC HW decode unsupported"; return }
        do {
            let l = try NWListener(using: .tcp, on: NWEndpoint.Port(rawValue: port)!)
            l.stateUpdateHandler = { [weak self] state in
                Task { @MainActor in
                    if case .ready = state { self?.status = "Waiting for host (port \(self?.port ?? 0))" }
                    if case .failed(let e) = state { self?.status = "Listen failed: \(e)" }
                }
            }
            l.newConnectionHandler = { [weak self] conn in Task { @MainActor in self?.accept(conn) } }
            l.start(queue: .main)
            listener = l
        } catch {
            status = "Listen failed: \(error)"
        }
    }

    private func accept(_ conn: NWConnection) {
        connection = conn
        status = "Host connected"
        conn.start(queue: .main)
        // Announce capabilities and ask for a keyframe to start.
        send(.hello(Hello(codecMask: Wire.maskHEVC | Wire.maskH264,
                          maxWidth: 2360, maxHeight: 1640, maxBitrateKbps: 80000, maxFps: 60)))
        send(.keyframeRequest(KeyframeRequest(reason: 0, lastGoodSeq: 0)))
        receive(conn)
    }

    private func receive(_ conn: NWConnection) {
        conn.receive(minimumIncompleteLength: 1, maximumLength: 1 << 18) { [weak self] data, _, isComplete, error in
            Task { @MainActor in
                guard let self else { return }
                if let data, !data.isEmpty { self.process(data) }
                if isComplete || error != nil {
                    self.status = "Disconnected — replug cable"
                    conn.cancel()
                } else {
                    self.receive(conn)
                }
            }
        }
    }

    private func process(_ data: Data) {
        parser.feed(data)
        while true {
            switch parser.next() {
            case .success(nil):
                return
            case .failure(let e):
                status = "Protocol error: \(e) — tearing down"
                connection?.cancel()
                return
            case .success(let raw?):
                switch decodeMessage(raw) {
                case .success(.videoFrame(let vf)):
                    decoder.decode(vf.bitstream)
                case .success(.welcome(let w)):
                    status = "Negotiated \(w.width)×\(w.height) @\(w.fps)"
                case .success(.ping(let p)):
                    send(.pong(Pong(token: p.token)))
                case .success(.disconnect):
                    connection?.cancel()
                case .failure(let e):
                    status = "Protocol violation: \(e) — tearing down"
                    connection?.cancel()
                    return
                default:
                    break
                }
            }
        }
    }

    private func send(_ msg: Message) {
        connection?.send(content: encode(msg), completion: .contentProcessed { _ in })
    }
}

struct M3ContentView: View {
    @StateObject private var session = VideoSession()

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            MetalVideoView { renderer in session.renderer = renderer }
                .ignoresSafeArea()
            VStack {
                Spacer()
                Text(session.status).font(.footnote.monospaced())
                Text("frames decoded: \(session.framesDecoded)").font(.footnote.monospaced())
            }
            .padding()
            .foregroundStyle(.white)
        }
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true  // keep awake while connected (§7.4)
            session.start()
        }
    }
}
