// SecureTransport (M5, iOS): runs the TOFU pairing handshake over an NWConnection, then transparently
// AEAD-encrypts/decrypts the byte stream. Mirrors windows-host/crypto/SecureByteChannel's framing
// ([u32 len LE][payload] records). After establish(), VideoSession feeds decrypted bytes to its §5
// parser and sends §5 messages via send(). ⛔ Built on a Mac; verified on the iPad.
import Foundation
import Network
import Sodium

final class SecureTransport {
    enum Result { case ok, peerRejected, badSignature, protocolError, ioError }

    private let connection: NWConnection
    private let me: DeviceIdentity
    private let pairing: Pairing
    private var channel: SecureChannel?
    private var buffer = [UInt8]()

    init(connection: NWConnection, identity: DeviceIdentity) {
        self.connection = connection
        self.me = identity
        self.pairing = Pairing(identity, role: .client)  // iPad = client
    }

    // Perform the 2-round handshake, then exchange secretstream headers. `pinned` is the host identity
    // we already trust (nil on first pairing → learned + returned for the caller to persist).
    func establish(pinned: Bytes?, completion: @escaping (Result, _ learnedPeer: Bytes?) -> Void) {
        sendFramed(pairing.hello())
        readFrame { [weak self] peerHello in
            guard let self, let peerHello, self.pairing.consumePeerHello(peerHello) else {
                completion(.ioError, nil); return
            }
            self.sendFramed(self.pairing.confirm())
            self.readFrame { peerConfirm in
                guard let peerConfirm else { completion(.ioError, nil); return }
                switch self.pairing.consumePeerConfirm(peerConfirm, pinned: pinned) {
                case .ok: break
                case .identityMismatch: completion(.peerRejected, nil); return
                case .badSignature: completion(.badSignature, nil); return
                case .protocolError: completion(.protocolError, nil); return
                }
                let learned = pinned == nil ? self.pairing.peerIdentity : nil
                guard let ch = self.pairing.makeChannel() else { completion(.protocolError, nil); return }
                self.channel = ch
                self.sendFramed(ch.txHeader)
                self.readFrame { peerHeader in
                    guard let peerHeader, ch.initRx(peerHeader: peerHeader) else {
                        completion(.ioError, nil); return
                    }
                    completion(.ok, learned)
                }
            }
        }
    }

    // Encrypt + frame + send one plaintext message (a §5 frame).
    func send(_ plaintext: [UInt8]) {
        guard let record = channel?.encrypt(plaintext) else { return }
        sendFramed(record)
    }

    // Continuously read records, decrypt, and hand the plaintext to `onPlaintext`. On tamper/close it
    // calls `onClosed`.
    func receiveLoop(onPlaintext: @escaping ([UInt8]) -> Void, onClosed: @escaping () -> Void) {
        readFrame { [weak self] record in
            guard let self else { return }
            guard let record, let plain = self.channel?.decrypt(record) else { onClosed(); return }
            onPlaintext(plain)
            self.receiveLoop(onPlaintext: onPlaintext, onClosed: onClosed)
        }
    }

    // MARK: framing

    private func sendFramed(_ payload: [UInt8]) {
        let n = UInt32(payload.count)
        let hdr: [UInt8] = [UInt8(n & 0xFF), UInt8((n >> 8) & 0xFF), UInt8((n >> 16) & 0xFF), UInt8((n >> 24) & 0xFF)]
        connection.send(content: Data(hdr + payload), completion: .contentProcessed { _ in })
    }

    // A bit over the protocol's 16 MiB payload cap; matches the host (SecureByteChannel::kMaxFrame).
    private static let maxFrame = 17 * 1024 * 1024
    private enum Extract { case frame([UInt8]); case needMore; case error }

    private func extractFrame() -> Extract {
        guard buffer.count >= 4 else { return .needMore }
        let len = Int(buffer[0]) | (Int(buffer[1]) << 8) | (Int(buffer[2]) << 16) | (Int(buffer[3]) << 24)
        // Reject absurd/oversized lengths: the length is plaintext and arrives BEFORE authentication,
        // so an unbounded read here is a pre-auth OOM DoS.
        guard len > 0, len <= Self.maxFrame else { return .error }
        guard buffer.count >= 4 + len else { return .needMore }
        let payload = Array(buffer[4..<(4 + len)])
        buffer.removeFirst(4 + len)
        return .frame(payload)
    }

    // One outstanding receive at a time (handshake is sequential; receiveLoop re-arms after each frame).
    private func readFrame(_ done: @escaping ([UInt8]?) -> Void) {
        switch extractFrame() {
        case .frame(let f): done(f); return
        case .error: done(nil); return
        case .needMore: break
        }
        connection.receive(minimumIncompleteLength: 1, maximumLength: 1 << 18) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            if let data, !data.isEmpty { self.buffer.append(contentsOf: [UInt8](data)) }
            switch self.extractFrame() {
            case .frame(let f): done(f)
            case .error: done(nil)
            case .needMore:
                if isComplete || error != nil { done(nil) } else { self.readFrame(done) }
            }
        }
    }
}
