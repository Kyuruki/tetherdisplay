// iOS Keychain persistence for the device identity + the pinned host identity (M5). Generic-password
// items, AfterFirstUnlockThisDeviceOnly — no paid entitlement needed (free personal-team OK).
// ⛔ Built on a Mac; verified on the iPad.
import Foundation
import Security
import Sodium

enum KeychainStore {
    private static let service = "com.tetherdisplay.m5.identity"

    private static func read(_ account: String) -> Bytes? {
        let q: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var out: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let data = out as? Data
        else { return nil }
        return [UInt8](data)
    }

    private static func write(_ account: String, _ value: Bytes) {
        let data = Data(value)
        let base: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        var add = base
        add[kSecValueData as String] = data
        add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        let status = SecItemAdd(add as CFDictionary, nil)
        if status == errSecDuplicateItem {
            SecItemUpdate(base as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        }
    }

    static func loadOrCreateIdentity() -> DeviceIdentity {
        if let pk = read("self_pk"), let sk = read("self_sk") {
            return DeviceIdentity(signPublicKey: pk, signSecretKey: sk)
        }
        let id = DeviceIdentity.generate()
        write("self_pk", id.signPublicKey)
        write("self_sk", id.signSecretKey)
        return id
    }

    static func loadPinnedPeer() -> Bytes? { read("peer_pk") }
    static func savePinnedPeer(_ pk: Bytes) { write("peer_pk", pk) }
}
