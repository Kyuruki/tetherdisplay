// Windows Credential Manager-backed identity store (M5, production host). DPAPI-protected per-user
// generic credentials. Windows only. See docs/SECURITY.md for the at-rest threat model (readable by
// same-user processes — encryption-at-rest vs other users, not anti-malware).
#ifndef TD_CRYPTO_CREDENTIAL_STORE_HPP
#define TD_CRYPTO_CREDENTIAL_STORE_HPP
#if defined(_WIN32)

#include "td/crypto/identity_store.hpp"

namespace td::crypto {

class CredentialManagerStore : public IIdentityStore {
 public:
  std::optional<Identity> LoadSelf() override;
  bool SaveSelf(const Identity& id) override;
  std::optional<PublicKey> LoadPeer() override;
  bool SavePeer(const PublicKey& peer) override;
};

}  // namespace td::crypto

#endif  // _WIN32
#endif  // TD_CRYPTO_CREDENTIAL_STORE_HPP
