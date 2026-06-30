// Persistent storage for the device's long-term identity + the pinned peer identity (M5). The
// interface is abstract; production uses CredentialManagerStore (Windows) / Keychain (iOS), while a
// FileIdentityStore exists for dev/tests. See docs/SECURITY.md.
#ifndef TD_CRYPTO_IDENTITY_STORE_HPP
#define TD_CRYPTO_IDENTITY_STORE_HPP

#include "td/crypto/secure_channel.hpp"

#include <optional>
#include <string>

namespace td::crypto {

class IIdentityStore {
 public:
  virtual ~IIdentityStore() = default;
  virtual std::optional<Identity> LoadSelf() = 0;
  virtual bool SaveSelf(const Identity& id) = 0;
  virtual std::optional<PublicKey> LoadPeer() = 0;  // the pinned peer, nullopt if not yet paired
  virtual bool SavePeer(const PublicKey& peer) = 0;
};

// Return this device's identity, generating + persisting a new one on first run.
Identity LoadOrCreateIdentity(IIdentityStore& store);

// Dev/portable store backed by a binary file. NOT hardware-protected — production hosts use
// CredentialManagerStore (DPAPI) and iPads use the Keychain. Useful for CI/WSL tests.
class FileIdentityStore : public IIdentityStore {
 public:
  explicit FileIdentityStore(std::string path) : path_(std::move(path)) {}
  std::optional<Identity> LoadSelf() override;
  bool SaveSelf(const Identity& id) override;
  std::optional<PublicKey> LoadPeer() override;
  bool SavePeer(const PublicKey& peer) override;

 private:
  std::string path_;
};

}  // namespace td::crypto

#endif  // TD_CRYPTO_IDENTITY_STORE_HPP
