// Integration tests for SecureByteChannel + FileIdentityStore over a linked in-memory pipe, with the
// two parties pairing concurrently on threads. Proves: encrypted round-trips; reconnect verifies the
// pinned identity; and an UNKNOWN device is refused (the M5 security gate).
#include "td/crypto/secure_byte_channel.hpp"
#include "td/crypto/identity_store.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace td;
using namespace td::crypto;

namespace {

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// A bidirectional in-memory pipe with two endpoints (A and B).
struct Pipe {
  std::mutex m;
  std::condition_variable cv;
  std::deque<std::uint8_t> a_to_b, b_to_a;
  bool open = true;
};

class Endpoint : public core::IByteChannel {
 public:
  Endpoint(Pipe& p, bool is_a) : p_(p), is_a_(is_a) {}
  bool SendAll(std::span<const std::uint8_t> d) override {
    std::lock_guard<std::mutex> l(p_.m);
    if (!p_.open) return false;
    auto& q = is_a_ ? p_.a_to_b : p_.b_to_a;
    q.insert(q.end(), d.begin(), d.end());
    p_.cv.notify_all();
    return true;
  }
  std::int64_t Recv(std::span<std::uint8_t> buf) override {
    std::unique_lock<std::mutex> l(p_.m);
    auto& q = is_a_ ? p_.b_to_a : p_.a_to_b;  // read what the other side sent
    p_.cv.wait(l, [&] { return !q.empty() || !p_.open; });
    if (q.empty()) return 0;
    const std::size_t n = std::min(buf.size(), q.size());
    std::copy_n(q.begin(), n, buf.data());
    q.erase(q.begin(), q.begin() + n);
    return static_cast<std::int64_t>(n);
  }
  void Close() override {
    std::lock_guard<std::mutex> l(p_.m);
    p_.open = false;
    p_.cv.notify_all();
  }

 private:
  Pipe& p_;
  bool is_a_;
};

struct CryptoEnv : ::testing::Environment {
  void SetUp() override { ASSERT_TRUE(InitCrypto()); }
};
const auto* kEnv = ::testing::AddGlobalTestEnvironment(new CryptoEnv);

// Read exactly n decrypted bytes from a channel (Recv may dribble).
bool recvN(core::IByteChannel& ch, std::size_t n, std::vector<std::uint8_t>& out) {
  out.assign(n, 0);
  std::size_t got = 0;
  while (got < n) {
    auto r = ch.Recv(std::span<std::uint8_t>(out.data() + got, n - got));
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

const char* kHostStore = "/tmp/td-test-host.store";
const char* kIpadStore = "/tmp/td-test-ipad.store";
void resetStores() { std::remove(kHostStore); std::remove(kIpadStore); }

}  // namespace

TEST(FileIdentityStore, RoundTrips) {
  std::remove("/tmp/td-idstore.bin");
  FileIdentityStore s("/tmp/td-idstore.bin");
  EXPECT_FALSE(s.LoadSelf());
  auto id = LoadOrCreateIdentity(s);            // generates + persists
  auto again = s.LoadSelf();
  ASSERT_TRUE(again);
  EXPECT_EQ(again->sign_pk, id.sign_pk);
  EXPECT_FALSE(s.LoadPeer());
  PublicKey peer{};
  peer[0] = 0xAB;
  EXPECT_TRUE(s.SavePeer(peer));
  EXPECT_EQ(s.LoadPeer(), std::optional<PublicKey>(peer));
  EXPECT_EQ(s.LoadSelf()->sign_pk, id.sign_pk);  // saving the peer preserved self
}

TEST(SecureByteChannel, PairThenEncryptedRoundTrip) {
  resetStores();
  Pipe pipe;
  Endpoint a(pipe, true), b(pipe, false);
  FileIdentityStore hs(kHostStore), is(kIpadStore);
  Identity host = LoadOrCreateIdentity(hs), ipad = LoadOrCreateIdentity(is);

  SecureByteChannel hostCh, ipadCh;
  HandshakeResult hr = HandshakeResult::IoError;
  std::thread t([&] { hr = hostCh.Establish(a, host, Pairing::Role::Server, hs); });
  HandshakeResult ir = ipadCh.Establish(b, ipad, Pairing::Role::Client, is);
  t.join();
  ASSERT_EQ(hr, HandshakeResult::Ok);
  ASSERT_EQ(ir, HandshakeResult::Ok);

  // host -> iPad
  ASSERT_TRUE(hostCh.SendAll(bytes("encrypted-video-frame")));
  std::vector<std::uint8_t> got;
  ASSERT_TRUE(recvN(ipadCh, bytes("encrypted-video-frame").size(), got));
  EXPECT_EQ(got, bytes("encrypted-video-frame"));

  // iPad -> host
  ASSERT_TRUE(ipadCh.SendAll(bytes("keyframe-request")));
  ASSERT_TRUE(recvN(hostCh, bytes("keyframe-request").size(), got));
  EXPECT_EQ(got, bytes("keyframe-request"));

  // The pairing was pinned on both sides (TOFU).
  EXPECT_EQ(hs.LoadPeer(), std::optional<PublicKey>(ipad.sign_pk));
  EXPECT_EQ(is.LoadPeer(), std::optional<PublicKey>(host.sign_pk));
  a.Close();
}

TEST(SecureByteChannel, ReconnectVerifiesPinnedIdentity) {
  resetStores();
  // First pairing to establish the pins.
  {
    Pipe pipe;
    Endpoint a(pipe, true), b(pipe, false);
    FileIdentityStore hs(kHostStore), is(kIpadStore);
    Identity host = LoadOrCreateIdentity(hs), ipad = LoadOrCreateIdentity(is);
    SecureByteChannel h, i;
    HandshakeResult hr;
    std::thread t([&] { hr = h.Establish(a, host, Pairing::Role::Server, hs); });
    auto ir = i.Establish(b, ipad, Pairing::Role::Client, is);
    t.join();
    ASSERT_EQ(hr, HandshakeResult::Ok);
    ASSERT_EQ(ir, HandshakeResult::Ok);
    a.Close();
  }
  // Reconnect with the SAME identities/stores — both are now pinned and must verify.
  Pipe pipe;
  Endpoint a(pipe, true), b(pipe, false);
  FileIdentityStore hs(kHostStore), is(kIpadStore);
  Identity host = LoadOrCreateIdentity(hs), ipad = LoadOrCreateIdentity(is);  // loaded, not regenerated
  SecureByteChannel h, i;
  HandshakeResult hr;
  std::thread t([&] { hr = h.Establish(a, host, Pairing::Role::Server, hs); });
  auto ir = i.Establish(b, ipad, Pairing::Role::Client, is);
  t.join();
  EXPECT_EQ(hr, HandshakeResult::Ok);
  EXPECT_EQ(ir, HandshakeResult::Ok);
  a.Close();
}

TEST(SecureByteChannel, UnknownDeviceRefused) {
  resetStores();
  // Host has already paired with the known iPad (pin it directly).
  FileIdentityStore hs(kHostStore);
  Identity host = LoadOrCreateIdentity(hs);
  Identity knownIpad = GenerateIdentity();
  hs.SavePeer(knownIpad.sign_pk);

  // A STRANGER iPad (different identity) connects.
  Pipe pipe;
  Endpoint a(pipe, true), b(pipe, false);
  std::remove("/tmp/td-stranger.store");
  FileIdentityStore ss("/tmp/td-stranger.store");
  Identity stranger = LoadOrCreateIdentity(ss);

  SecureByteChannel h, s;
  HandshakeResult hr = HandshakeResult::IoError, sr = HandshakeResult::IoError;
  std::thread t([&] { sr = s.Establish(b, stranger, Pairing::Role::Client, ss); });
  hr = h.Establish(a, host, Pairing::Role::Server, hs);
  // The host refuses the unknown device.
  EXPECT_EQ(hr, HandshakeResult::PeerRejected);
  a.Close();  // unblock the stranger's pending read so its thread can finish
  b.Close();
  t.join();
}
