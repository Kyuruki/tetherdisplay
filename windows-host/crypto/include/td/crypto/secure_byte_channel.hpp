// SecureByteChannel (M5): wraps a raw byte channel (the usbmux tunnel), runs the TOFU pairing
// handshake, then transparently AEAD-encrypts every byte. Drop-in IByteChannel for the streaming
// session — neither side's framing code knows encryption is happening. See docs/SECURITY.md.
#ifndef TD_CRYPTO_SECURE_BYTE_CHANNEL_HPP
#define TD_CRYPTO_SECURE_BYTE_CHANNEL_HPP

#include "td/core/byte_channel.hpp"
#include "td/crypto/identity_store.hpp"
#include "td/crypto/secure_channel.hpp"

#include <cstdint>
#include <vector>

namespace td::crypto {

enum class HandshakeResult { Ok, PeerRejected, BadSignature, Protocol, IoError };
const char* to_string(HandshakeResult r);

class SecureByteChannel : public core::IByteChannel {
 public:
  // Pair over `raw` (role: host = Server, iPad = Client) and set up the AEAD channel. On first pairing
  // the peer identity is learned and pinned via `store`; on reconnect it must match the pinned id
  // (an unknown device returns PeerRejected). After Ok, use this object as the encrypted channel.
  HandshakeResult Establish(core::IByteChannel& raw, const Identity& me, Pairing::Role role,
                            IIdentityStore& store);

  bool SendAll(std::span<const std::uint8_t> data) override;
  std::int64_t Recv(std::span<std::uint8_t> buf) override;
  void Close() override;

 private:
  bool ReadFramed(std::vector<std::uint8_t>& out);              // read one [u32 len][payload] from raw_
  bool WriteFramed(std::span<const std::uint8_t> payload);
  bool RecvExact(std::uint8_t* p, std::size_t n);

  core::IByteChannel* raw_ = nullptr;
  SecureChannel channel_;
  std::vector<std::uint8_t> rx_plain_;  // decrypted bytes not yet returned by Recv
  std::size_t rx_pos_ = 0;
  bool broken_ = false;                 // latched on a decrypt failure — tamper is terminal
};

}  // namespace td::crypto

#endif  // TD_CRYPTO_SECURE_BYTE_CHANNEL_HPP
