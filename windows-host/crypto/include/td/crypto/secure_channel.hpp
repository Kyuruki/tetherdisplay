// TOFU pairing + AEAD channel (M5), libsodium. Ed25519 long-term identity (the TOFU-pinned device id) +
// a per-connection ephemeral X25519 (crypto_kx) exchange whose transcript is signed by the identity key
// (forward secrecy, MITM resistance, anti-replay), then two independent crypto_secretstream AEAD streams
// (one per direction). Verified design in docs/SECURITY.md. Pure crypto (no socket I/O) — unit-tested.
#ifndef TD_CRYPTO_SECURE_CHANNEL_HPP
#define TD_CRYPTO_SECURE_CHANNEL_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <sodium.h>

namespace td::crypto {

// Call once at startup (idempotent). Returns false if libsodium failed to initialize.
bool InitCrypto();

using PublicKey = std::array<std::uint8_t, 32>;  // Ed25519 identity public key = the TOFU device id

// A device's long-term identity (Ed25519). The secret key never leaves secure storage.
struct Identity {
  std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> sign_pk{};
  std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> sign_sk{};
};
Identity GenerateIdentity();
Identity IdentityFromSeed(std::span<const std::uint8_t> seed32);  // deterministic (for tests)

// The post-handshake encrypted channel: a push stream (our tx) + a pull stream (our rx). Each side
// sends TxHeader() first, then InitRx(peer header); thereafter Encrypt/Decrypt records.
class SecureChannel {
 public:
  SecureChannel() = default;
  SecureChannel(const std::array<std::uint8_t, 32>& tx_key, const std::array<std::uint8_t, 32>& rx_key);

  bool valid() const { return push_inited_; }
  // The 24-byte secretstream header for our outbound stream — send this first.
  std::vector<std::uint8_t> TxHeader() const { return tx_header_; }
  // Ingest the peer's header to start decrypting their stream. False if malformed.
  bool InitRx(std::span<const std::uint8_t> peer_header);

  // Encrypt one record (ciphertext = plaintext + crypto_secretstream_..._ABYTES).
  std::vector<std::uint8_t> Encrypt(std::span<const std::uint8_t> plaintext);
  // Decrypt one record; nullopt on tampering/truncation (Poly1305 tag failure).
  std::optional<std::vector<std::uint8_t>> Decrypt(std::span<const std::uint8_t> record);

 private:
  crypto_secretstream_xchacha20poly1305_state push_state_{};
  crypto_secretstream_xchacha20poly1305_state pull_state_{};
  std::array<std::uint8_t, 32> rx_key_{};
  std::vector<std::uint8_t> tx_header_;
  bool push_inited_ = false;
  bool pull_inited_ = false;
};

// Drives the 2-round signed-ephemeral handshake. Caller does the I/O: send Hello(), feed the peer's
// Hello to ConsumePeerHello(), send Confirm(), feed the peer's Confirm to ConsumePeerConfirm().
class Pairing {
 public:
  enum class Role { Server, Client };  // host = Server, iPad = Client (fixed)
  enum class Verdict { Ok, IdentityMismatch, BadSignature, Protocol };

  Pairing(const Identity& me, Role role);

  // [ephemeral_kx_pk(32)][identity_pk(32)]
  std::vector<std::uint8_t> Hello() const;
  // Store the peer's ephemeral + identity. False if malformed.
  bool ConsumePeerHello(std::span<const std::uint8_t> bytes);
  // [signature(64)] over the transcript. Call after ConsumePeerHello().
  std::vector<std::uint8_t> Confirm() const;
  // Verify the peer's signature against `pinned` (if set — a known device; mismatch => refused) or
  // accept-and-learn on first pairing (pinned == nullopt). On Ok, MakeChannel() is valid.
  Verdict ConsumePeerConfirm(std::span<const std::uint8_t> bytes,
                             const std::optional<PublicKey>& pinned);

  PublicKey PeerIdentity() const { return peer_ident_; }  // pin this on first pairing
  SecureChannel MakeChannel() const;                      // valid only after Verdict::Ok

 private:
  std::vector<std::uint8_t> Transcript() const;

  Identity me_;
  Role role_;
  std::array<std::uint8_t, crypto_kx_PUBLICKEYBYTES> eph_pk_{};
  std::array<std::uint8_t, crypto_kx_SECRETKEYBYTES> eph_sk_{};
  std::array<std::uint8_t, crypto_kx_PUBLICKEYBYTES> peer_eph_{};
  PublicKey peer_ident_{};
  bool have_peer_hello_ = false;
  bool confirmed_ = false;
};

}  // namespace td::crypto

#endif  // TD_CRYPTO_SECURE_CHANNEL_HPP
