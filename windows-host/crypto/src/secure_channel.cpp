// TOFU pairing + AEAD channel — libsodium implementation. See secure_channel.hpp + docs/SECURITY.md.
#include "td/crypto/secure_channel.hpp"

#include <algorithm>
#include <cstring>

namespace td::crypto {
namespace {

// Domain-separation label folded into the signed transcript (bumping it breaks cross-version pairing).
constexpr char kLabel[] = "TetherDisplay-v2-pairing";
constexpr std::size_t kHelloSize = crypto_kx_PUBLICKEYBYTES + crypto_sign_PUBLICKEYBYTES;  // 64

void append(std::vector<std::uint8_t>& v, std::span<const std::uint8_t> b) {
  v.insert(v.end(), b.begin(), b.end());
}

}  // namespace

bool InitCrypto() { return sodium_init() >= 0; }

Identity GenerateIdentity() {
  Identity id;
  crypto_sign_keypair(id.sign_pk.data(), id.sign_sk.data());
  return id;
}

Identity IdentityFromSeed(std::span<const std::uint8_t> seed32) {
  Identity id;
  std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed{};
  std::memcpy(seed.data(), seed32.data(),
              std::min(seed.size(), seed32.size()));
  crypto_sign_seed_keypair(id.sign_pk.data(), id.sign_sk.data(), seed.data());
  return id;
}

// ---- SecureChannel ----

SecureChannel::SecureChannel(const std::array<std::uint8_t, 32>& tx_key,
                             const std::array<std::uint8_t, 32>& rx_key)
    : rx_key_(rx_key) {
  tx_header_.resize(crypto_secretstream_xchacha20poly1305_HEADERBYTES);
  crypto_secretstream_xchacha20poly1305_init_push(&push_state_, tx_header_.data(), tx_key.data());
  push_inited_ = true;
}

bool SecureChannel::InitRx(std::span<const std::uint8_t> peer_header) {
  if (peer_header.size() != crypto_secretstream_xchacha20poly1305_HEADERBYTES) return false;
  if (crypto_secretstream_xchacha20poly1305_init_pull(&pull_state_, peer_header.data(),
                                                      rx_key_.data()) != 0)
    return false;
  pull_inited_ = true;
  return true;
}

std::vector<std::uint8_t> SecureChannel::Encrypt(std::span<const std::uint8_t> plaintext) {
  std::vector<std::uint8_t> out(plaintext.size() + crypto_secretstream_xchacha20poly1305_ABYTES);
  unsigned long long clen = 0;
  crypto_secretstream_xchacha20poly1305_push(
      &push_state_, out.data(), &clen, plaintext.data(), plaintext.size(), nullptr, 0,
      crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
  out.resize(static_cast<std::size_t>(clen));
  return out;
}

std::optional<std::vector<std::uint8_t>> SecureChannel::Decrypt(std::span<const std::uint8_t> record) {
  if (!pull_inited_ || record.size() < crypto_secretstream_xchacha20poly1305_ABYTES)
    return std::nullopt;
  std::vector<std::uint8_t> out(record.size() - crypto_secretstream_xchacha20poly1305_ABYTES);
  unsigned long long mlen = 0;
  unsigned char tag = 0;
  if (crypto_secretstream_xchacha20poly1305_pull(&pull_state_, out.data(), &mlen, &tag, record.data(),
                                                 record.size(), nullptr, 0) != 0)
    return std::nullopt;  // tampered/out-of-order/truncated
  out.resize(static_cast<std::size_t>(mlen));
  return out;
}

// ---- Pairing ----

Pairing::Pairing(const Identity& me, Role role) : me_(me), role_(role) {
  crypto_kx_keypair(eph_pk_.data(), eph_sk_.data());
}

std::vector<std::uint8_t> Pairing::Hello() const {
  std::vector<std::uint8_t> out;
  out.reserve(kHelloSize);
  append(out, eph_pk_);
  append(out, me_.sign_pk);
  return out;
}

bool Pairing::ConsumePeerHello(std::span<const std::uint8_t> bytes) {
  if (bytes.size() != kHelloSize) return false;
  std::memcpy(peer_eph_.data(), bytes.data(), crypto_kx_PUBLICKEYBYTES);
  std::memcpy(peer_ident_.data(), bytes.data() + crypto_kx_PUBLICKEYBYTES,
              crypto_sign_PUBLICKEYBYTES);
  have_peer_hello_ = true;
  return true;
}

std::vector<std::uint8_t> Pairing::Transcript() const {
  // Canonical order: server identity, client identity, server ephemeral, client ephemeral.
  const auto& server_ident = (role_ == Role::Server) ? me_.sign_pk : peer_ident_;
  const auto& client_ident = (role_ == Role::Server) ? peer_ident_ : me_.sign_pk;
  const auto& server_eph = (role_ == Role::Server) ? eph_pk_ : peer_eph_;
  const auto& client_eph = (role_ == Role::Server) ? peer_eph_ : eph_pk_;

  std::vector<std::uint8_t> t;
  append(t, {reinterpret_cast<const std::uint8_t*>(kLabel), sizeof(kLabel) - 1});
  append(t, server_ident);
  append(t, client_ident);
  append(t, server_eph);
  append(t, client_eph);
  return t;
}

std::vector<std::uint8_t> Pairing::Confirm() const {
  auto transcript = Transcript();
  std::vector<std::uint8_t> sig(crypto_sign_BYTES);
  unsigned long long siglen = 0;
  crypto_sign_detached(sig.data(), &siglen, transcript.data(), transcript.size(), me_.sign_sk.data());
  sig.resize(static_cast<std::size_t>(siglen));
  return sig;
}

Pairing::Verdict Pairing::ConsumePeerConfirm(std::span<const std::uint8_t> bytes,
                                             const std::optional<PublicKey>& pinned) {
  if (!have_peer_hello_ || bytes.size() != crypto_sign_BYTES) return Verdict::Protocol;
  // Refuse an unknown/unexpected device BEFORE trusting anything it signed.
  if (pinned && *pinned != peer_ident_) return Verdict::IdentityMismatch;

  auto transcript = Transcript();
  if (crypto_sign_verify_detached(bytes.data(), transcript.data(), transcript.size(),
                                  peer_ident_.data()) != 0)
    return Verdict::BadSignature;

  confirmed_ = true;
  return Verdict::Ok;
}

SecureChannel Pairing::MakeChannel() const {
  std::array<std::uint8_t, crypto_kx_SESSIONKEYBYTES> rx{}, tx{};
  const int rc = (role_ == Role::Server)
                     ? crypto_kx_server_session_keys(rx.data(), tx.data(), eph_pk_.data(),
                                                     eph_sk_.data(), peer_eph_.data())
                     : crypto_kx_client_session_keys(rx.data(), tx.data(), eph_pk_.data(),
                                                     eph_sk_.data(), peer_eph_.data());
  if (rc != 0) return SecureChannel();  // peer ephemeral was degenerate -> invalid channel
  return SecureChannel(tx, rx);  // encrypt with tx, decrypt with rx (client_tx == server_rx, etc.)
}

}  // namespace td::crypto
