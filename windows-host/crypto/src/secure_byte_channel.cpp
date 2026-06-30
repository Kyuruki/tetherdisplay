// SecureByteChannel — pairing handshake + AEAD framing over a raw byte channel. See header.
#include "td/crypto/secure_byte_channel.hpp"

#include <cstring>

namespace td::crypto {
namespace {
constexpr std::size_t kMaxFrame = 17 * 1024 * 1024;  // a bit over the protocol's 16 MiB payload cap
}

const char* to_string(HandshakeResult r) {
  switch (r) {
    case HandshakeResult::Ok: return "Ok";
    case HandshakeResult::PeerRejected: return "PeerRejected";
    case HandshakeResult::BadSignature: return "BadSignature";
    case HandshakeResult::Protocol: return "Protocol";
    case HandshakeResult::IoError: return "IoError";
  }
  return "Unknown";
}

bool SecureByteChannel::RecvExact(std::uint8_t* p, std::size_t n) {
  std::size_t got = 0;
  while (got < n) {
    const std::int64_t r = raw_->Recv(std::span<std::uint8_t>(p + got, n - got));
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

bool SecureByteChannel::ReadFramed(std::vector<std::uint8_t>& out) {
  std::uint8_t hdr[4];
  if (!RecvExact(hdr, 4)) return false;
  const std::uint32_t len = static_cast<std::uint32_t>(hdr[0]) | (static_cast<std::uint32_t>(hdr[1]) << 8) |
                            (static_cast<std::uint32_t>(hdr[2]) << 16) |
                            (static_cast<std::uint32_t>(hdr[3]) << 24);
  if (len == 0 || len > kMaxFrame) return false;
  out.resize(len);
  return RecvExact(out.data(), len);
}

bool SecureByteChannel::WriteFramed(std::span<const std::uint8_t> payload) {
  const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
  std::uint8_t hdr[4] = {static_cast<std::uint8_t>(len & 0xFF),
                         static_cast<std::uint8_t>((len >> 8) & 0xFF),
                         static_cast<std::uint8_t>((len >> 16) & 0xFF),
                         static_cast<std::uint8_t>((len >> 24) & 0xFF)};
  if (!raw_->SendAll(std::span<const std::uint8_t>(hdr, 4))) return false;
  return raw_->SendAll(payload);
}

HandshakeResult SecureByteChannel::Establish(core::IByteChannel& raw, const Identity& me,
                                             Pairing::Role role, IIdentityStore& store) {
  raw_ = &raw;
  Pairing pairing(me, role);

  // Round 1: exchange Hello (ephemeral kx pubkey + identity pubkey).
  if (!WriteFramed(pairing.Hello())) return HandshakeResult::IoError;
  std::vector<std::uint8_t> peer_hello;
  if (!ReadFramed(peer_hello)) return HandshakeResult::IoError;
  if (!pairing.ConsumePeerHello(peer_hello)) return HandshakeResult::Protocol;

  // Round 2: exchange Confirm (signature over the transcript).
  if (!WriteFramed(pairing.Confirm())) return HandshakeResult::IoError;
  std::vector<std::uint8_t> peer_confirm;
  if (!ReadFramed(peer_confirm)) return HandshakeResult::IoError;

  const std::optional<PublicKey> pinned = store.LoadPeer();
  switch (pairing.ConsumePeerConfirm(peer_confirm, pinned)) {
    case Pairing::Verdict::Ok: break;
    case Pairing::Verdict::IdentityMismatch: return HandshakeResult::PeerRejected;
    case Pairing::Verdict::BadSignature: return HandshakeResult::BadSignature;
    case Pairing::Verdict::Protocol: return HandshakeResult::Protocol;
  }
  // TOFU: pin the peer on first pairing. Fail closed if we can't persist it — otherwise the next
  // connection would re-enter first-use and silently trust any device.
  if (!pinned && !store.SavePeer(pairing.PeerIdentity())) return HandshakeResult::IoError;

  channel_ = pairing.MakeChannel();
  if (!channel_.valid()) return HandshakeResult::Protocol;

  // Exchange the per-direction secretstream headers.
  if (!WriteFramed(channel_.TxHeader())) return HandshakeResult::IoError;
  std::vector<std::uint8_t> peer_header;
  if (!ReadFramed(peer_header)) return HandshakeResult::IoError;
  if (!channel_.InitRx(peer_header)) return HandshakeResult::Protocol;

  return HandshakeResult::Ok;
}

bool SecureByteChannel::SendAll(std::span<const std::uint8_t> data) {
  if (!raw_ || !channel_.valid() || broken_) return false;
  return WriteFramed(channel_.Encrypt(data));  // one AEAD record per SendAll
}

std::int64_t SecureByteChannel::Recv(std::span<std::uint8_t> buf) {
  if (!raw_) return -1;
  if (broken_) return -1;  // tamper already detected -> terminal (pull state is desynced)
  // Drain any leftover plaintext from a previous record first.
  while (rx_pos_ >= rx_plain_.size()) {
    std::vector<std::uint8_t> record;
    if (!ReadFramed(record)) return 0;  // peer closed / I/O end
    auto plain = channel_.Decrypt(record);
    if (!plain) { broken_ = true; return -1; }  // tampered/out-of-order -> terminal
    rx_plain_ = std::move(*plain);
    rx_pos_ = 0;
    if (rx_plain_.empty()) continue;  // skip empty records
  }
  const std::size_t n = std::min(buf.size(), rx_plain_.size() - rx_pos_);
  std::memcpy(buf.data(), rx_plain_.data() + rx_pos_, n);
  rx_pos_ += n;
  return static_cast<std::int64_t>(n);
}

void SecureByteChannel::Close() {
  if (raw_) raw_->Close();
}

}  // namespace td::crypto
