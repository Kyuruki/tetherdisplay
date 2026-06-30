// CredentialManagerStore — Windows Credential Manager. See header. ⛔ Win32, built on Windows.
#if defined(_WIN32)

#include "td/crypto/credential_store.hpp"

#include <windows.h>
#include <wincred.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

namespace td::crypto {
namespace {

constexpr wchar_t kSelfTarget[] = L"TetherDisplay_DeviceKey";
constexpr wchar_t kPeerTarget[] = L"TetherDisplay_PeerKey";

bool ReadBlob(const wchar_t* target, std::vector<std::uint8_t>& out) {
  PCREDENTIALW cred = nullptr;
  if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &cred)) return false;
  out.assign(cred->CredentialBlob, cred->CredentialBlob + cred->CredentialBlobSize);
  CredFree(cred);
  return true;
}

bool WriteBlob(const wchar_t* target, const std::uint8_t* data, std::size_t size) {
  CREDENTIALW cred{};
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = const_cast<LPWSTR>(target);
  cred.CredentialBlobSize = static_cast<DWORD>(size);
  cred.CredentialBlob = const_cast<LPBYTE>(data);
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  return CredWriteW(&cred, 0) == TRUE;
}

}  // namespace

std::optional<Identity> CredentialManagerStore::LoadSelf() {
  std::vector<std::uint8_t> blob;
  if (!ReadBlob(kSelfTarget, blob)) return std::nullopt;
  if (blob.size() != crypto_sign_PUBLICKEYBYTES + crypto_sign_SECRETKEYBYTES) return std::nullopt;
  Identity id;
  std::memcpy(id.sign_pk.data(), blob.data(), crypto_sign_PUBLICKEYBYTES);
  std::memcpy(id.sign_sk.data(), blob.data() + crypto_sign_PUBLICKEYBYTES, crypto_sign_SECRETKEYBYTES);
  return id;
}

bool CredentialManagerStore::SaveSelf(const Identity& id) {
  std::vector<std::uint8_t> blob;
  blob.insert(blob.end(), id.sign_pk.begin(), id.sign_pk.end());
  blob.insert(blob.end(), id.sign_sk.begin(), id.sign_sk.end());
  return WriteBlob(kSelfTarget, blob.data(), blob.size());
}

std::optional<PublicKey> CredentialManagerStore::LoadPeer() {
  std::vector<std::uint8_t> blob;
  if (!ReadBlob(kPeerTarget, blob) || blob.size() != 32) return std::nullopt;
  PublicKey pk;
  std::memcpy(pk.data(), blob.data(), 32);
  return pk;
}

bool CredentialManagerStore::SavePeer(const PublicKey& peer) {
  return WriteBlob(kPeerTarget, peer.data(), peer.size());
}

}  // namespace td::crypto

#endif  // _WIN32
