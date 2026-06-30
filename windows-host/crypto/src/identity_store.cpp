// FileIdentityStore + LoadOrCreateIdentity — portable. See identity_store.hpp.
#include "td/crypto/identity_store.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

namespace td::crypto {
namespace {

// File layout: [u8 has_self][pk(32)][sk(64)][u8 has_peer][peer(32)].
constexpr std::size_t kPkLen = crypto_sign_PUBLICKEYBYTES;   // 32
constexpr std::size_t kSkLen = crypto_sign_SECRETKEYBYTES;   // 64
constexpr std::size_t kRecordLen = 1 + kPkLen + kSkLen + 1 + 32;

struct Record {
  bool has_self = false;
  Identity self;
  bool has_peer = false;
  PublicKey peer{};
};

std::optional<Record> ReadRecord(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (b.size() != kRecordLen) return std::nullopt;
  Record r;
  std::size_t o = 0;
  r.has_self = b[o++] != 0;
  std::copy_n(b.begin() + o, kPkLen, r.self.sign_pk.begin()); o += kPkLen;
  std::copy_n(b.begin() + o, kSkLen, r.self.sign_sk.begin()); o += kSkLen;
  r.has_peer = b[o++] != 0;
  std::copy_n(b.begin() + o, 32, r.peer.begin());
  return r;
}

bool WriteRecord(const std::string& path, const Record& r) {
  std::vector<std::uint8_t> b;
  b.reserve(kRecordLen);
  b.push_back(r.has_self ? 1 : 0);
  b.insert(b.end(), r.self.sign_pk.begin(), r.self.sign_pk.end());
  b.insert(b.end(), r.self.sign_sk.begin(), r.self.sign_sk.end());
  b.push_back(r.has_peer ? 1 : 0);
  b.insert(b.end(), r.peer.begin(), r.peer.end());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  return f.good();
}

}  // namespace

Identity LoadOrCreateIdentity(IIdentityStore& store) {
  if (auto id = store.LoadSelf()) return *id;
  Identity id = GenerateIdentity();
  store.SaveSelf(id);
  return id;
}

std::optional<Identity> FileIdentityStore::LoadSelf() {
  auto r = ReadRecord(path_);
  if (r && r->has_self) return r->self;
  return std::nullopt;
}

bool FileIdentityStore::SaveSelf(const Identity& id) {
  Record r = ReadRecord(path_).value_or(Record{});  // preserve any existing peer
  r.has_self = true;
  r.self = id;
  return WriteRecord(path_, r);
}

std::optional<PublicKey> FileIdentityStore::LoadPeer() {
  auto r = ReadRecord(path_);
  if (r && r->has_peer) return r->peer;
  return std::nullopt;
}

bool FileIdentityStore::SavePeer(const PublicKey& peer) {
  Record r = ReadRecord(path_).value_or(Record{});  // preserve self
  r.has_peer = true;
  r.peer = peer;
  return WriteRecord(path_, r);
}

}  // namespace td::crypto
