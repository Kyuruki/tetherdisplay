// iOS mirror of the host TOFU pairing + AEAD channel (M5), using swift-sodium. Byte-for-byte
// compatible with windows-host/crypto (same Hello/Confirm/transcript/header layout) so host↔iPad
// interoperate. See docs/SECURITY.md.
//
// Dependency: swift-sodium (ISC) via SPM — add https://github.com/jedisct1/swift-sodium.
// ⛔ Built on a Mac; verified on the iPad (the agent cannot compile Swift). Marked [VERIFY-ON-HW].
import Foundation
import Sodium

private let sodium = Sodium()
private let kPairingLabel = Array("TetherDisplay-v2-pairing".utf8)

struct DeviceIdentity {
    var signPublicKey: Bytes   // 32 — the TOFU device id
    var signSecretKey: Bytes   // 64

    static func generate() -> DeviceIdentity {
        let kp = sodium.sign.keyPair()!
        return DeviceIdentity(signPublicKey: kp.publicKey, signSecretKey: kp.secretKey)
    }
}

// The post-handshake encrypted channel: a push stream (our tx) + a pull stream (our rx).
final class SecureChannel {
    private let push: SecretStream.XChaCha20Poly1305.PushStream
    private var pull: SecretStream.XChaCha20Poly1305.PullStream?
    let txHeader: Bytes

    init?(txKey: Bytes, rxKey: Bytes) {
        guard let p = sodium.secretStream.xchacha20poly1305.initPush(secretKey: txKey) else { return nil }
        push = p
        txHeader = p.header()
        self.rxKey = rxKey
    }
    private let rxKey: Bytes

    func initRx(peerHeader: Bytes) -> Bool {
        guard let p = sodium.secretStream.xchacha20poly1305.initPull(secretKey: rxKey, header: peerHeader)
        else { return false }
        pull = p
        return true
    }

    func encrypt(_ plaintext: Bytes) -> Bytes? { push.push(message: plaintext) }

    func decrypt(_ record: Bytes) -> Bytes? {
        guard let pull, let (message, _) = pull.pull(cipherText: record) else { return nil }
        return message
    }
}

// Drives the 2-round signed-ephemeral handshake. Caller does the I/O (see SecureTransport).
final class Pairing {
    enum Role { case server, client }   // host = server, iPad = client
    enum Verdict { case ok, identityMismatch, badSignature, protocolError }

    private let me: DeviceIdentity
    private let role: Role
    private let eph: KeyExchange.KeyPair
    private var peerEph = Bytes()
    private var peerIdent = Bytes()
    private(set) var havePeerHello = false

    init(_ me: DeviceIdentity, role: Role) {
        self.me = me
        self.role = role
        eph = sodium.keyExchange.keyPair()!
    }

    // [ephemeral_kx_pk(32)][identity_pk(32)]
    func hello() -> Bytes { eph.publicKey + me.signPublicKey }

    func consumePeerHello(_ bytes: Bytes) -> Bool {
        guard bytes.count == 64 else { return false }
        peerEph = Array(bytes[0..<32])
        peerIdent = Array(bytes[32..<64])
        havePeerHello = true
        return true
    }

    private func transcript() -> Bytes {
        let serverIdent = role == .server ? me.signPublicKey : peerIdent
        let clientIdent = role == .server ? peerIdent : me.signPublicKey
        let serverEph = role == .server ? eph.publicKey : peerEph
        let clientEph = role == .server ? peerEph : eph.publicKey
        return kPairingLabel + serverIdent + clientIdent + serverEph + clientEph
    }

    func confirm() -> Bytes { sodium.sign.signature(message: transcript(), secretKey: me.signSecretKey)! }

    func consumePeerConfirm(_ sig: Bytes, pinned: Bytes?) -> Verdict {
        guard havePeerHello, sig.count == 64 else { return .protocolError }
        if let pinned, pinned != peerIdent { return .identityMismatch }   // refuse unknown device
        guard sodium.sign.verify(message: transcript(), publicKey: peerIdent, signature: sig) else {
            return .badSignature
        }
        return .ok
    }

    var peerIdentity: Bytes { peerIdent }

    func makeChannel() -> SecureChannel? {
        let side: KeyExchange.Side = role == .server ? .SERVER : .CLIENT
        guard let session = sodium.keyExchange.sessionKeyPair(
            publicKey: eph.publicKey, secretKey: eph.secretKey, otherPublicKey: peerEph, side: side)
        else { return nil }
        return SecureChannel(txKey: session.tx, rxKey: session.rx)  // encrypt with tx, decrypt with rx
    }
}
