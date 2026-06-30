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
    private var transport: SecureTransport?
    private let identity = KeychainStore.loadOrCreateIdentity()
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
        status = "Pairing…"
        conn.start(queue: .main)
        let t = SecureTransport(connection: conn, identity: identity)
        transport = t
        // TOFU: verify against the pinned host identity, or learn it on first pairing.
        t.establish(pinned: KeychainStore.loadPinnedPeer()) { [weak self] result, learnedPeer in
            Task { @MainActor in
                guard let self else { return }
                guard result == .ok else {
                    self.status = "Pairing failed: \(result)"
                    conn.cancel()
                    return
                }
                if let learnedPeer { KeychainStore.savePinnedPeer(learnedPeer) }
                self.status = "Paired + encrypted"
                // Announce capabilities and ask for a keyframe to start (now over the encrypted channel).
                self.send(.hello(Hello(codecMask: Wire.maskHEVC | Wire.maskH264,
                                       maxWidth: 2360, maxHeight: 1640, maxBitrateKbps: 80000, maxFps: 60)))
                self.send(.keyframeRequest(KeyframeRequest(reason: 0, lastGoodSeq: 0)))
                t.receiveLoop(onPlaintext: { [weak self] bytes in
                    Task { @MainActor in self?.process(Data(bytes)) }
                }, onClosed: { [weak self] in
                    Task { @MainActor in self?.status = "Disconnected — replug cable" }
                })
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
        transport?.send([UInt8](encode(msg)))  // encrypted by the SecureTransport
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
