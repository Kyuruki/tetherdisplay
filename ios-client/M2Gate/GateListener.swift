// TetherDisplay — M2 gate (iOS side). A trivial app that accepts the usbmux-tunneled connection from
// the Windows host and shows the received byte count + FNV-1a checksum, so the operator can confirm
// bytes are flowing host -> iPad over the USB cable (the M2 gate).
//
// NOTE ON THE TRANSPORT CHOICE: the LOCKED decision (master prompt §7.2) is PeerTalk for the iOS
// client's USB transport, and the full client (M3+) will use it. For THIS trivial M2 receipt gate we
// use Apple's built-in Network.framework (NWListener) — no dependency to vendor, free-tier-compatible,
// and the usbmux tunnel arrives as a localhost connection on the device. The host's usbmux `Connect`
// PortNumber must match `gatePort` below.
//
// Build on a Mac (the agent cannot): drop this into a SwiftUI iOS app target, set free-personal-team
// signing, sideload via AltStore (see ios-client/README.md and docs/RUNBOOK.md M2).
import Network
import SwiftUI
import UIKit  // for UIApplication.isIdleTimerDisabled (not re-exported by SwiftUI)

@MainActor
final class GateListener: ObservableObject {
    @Published var bytes: UInt64 = 0
    @Published var checksum: UInt32 = 0x811C_9DC5  // FNV-1a offset basis (matches the host)
    @Published var status: String = "Starting…"

    private let gatePort: UInt16
    private var listener: NWListener?

    init(port: UInt16 = 2345) { self.gatePort = port }

    func start() {
        do {
            let l = try NWListener(using: .tcp, on: NWEndpoint.Port(rawValue: gatePort)!)
            l.stateUpdateHandler = { [weak self] state in
                Task { @MainActor in
                    switch state {
                    case .ready:  self?.status = "Waiting for host (port \(self?.gatePort ?? 0))"
                    case .failed(let e): self?.status = "Listen failed: \(e)"
                    default: break
                    }
                }
            }
            l.newConnectionHandler = { [weak self] conn in self?.accept(conn) }
            l.start(queue: .main)
            listener = l
        } catch {
            status = "Listen failed: \(error)"
        }
    }

    private func accept(_ conn: NWConnection) {
        status = "Host connected"
        conn.start(queue: .main)
        receive(conn)
    }

    private func receive(_ conn: NWConnection) {
        conn.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            Task { @MainActor in
                if let data, !data.isEmpty {
                    var h = self.checksum
                    for b in data { h ^= UInt32(b); h = h &* 0x0100_0193 }  // FNV-1a, 32-bit wrapping
                    self.checksum = h
                    self.bytes &+= UInt64(data.count)
                }
                if isComplete || error != nil {
                    self.status = "Disconnected — replug cable"
                    conn.cancel()
                } else {
                    self.receive(conn)
                }
            }
        }
    }
}

struct ContentView: View {
    @StateObject private var gate = GateListener()

    var body: some View {
        VStack(spacing: 20) {
            Text("TetherDisplay — M2 gate").font(.headline)
            Text(gate.status).foregroundStyle(.secondary)
            Text("Bytes received: \(gate.bytes)").font(.title2.monospaced())
            Text(String(format: "Checksum: 0x%08X", gate.checksum)).font(.title2.monospaced())
            Text("These must match the host's printed totals.")
                .font(.footnote).foregroundStyle(.secondary)
        }
        .padding()
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true  // keep the screen awake (§7.4)
            gate.start()
        }
    }
}
