// Security tests for the M5 TOFU pairing + AEAD channel, against real libsodium in WSL. Proves: a
// paired channel round-trips both directions; tampering is detected; an UNKNOWN/unpinned device is
// refused; a forged signature is rejected; and reconnect uses fresh keys (forward secrecy/anti-replay).
#include "td/crypto/secure_channel.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace td::crypto;

namespace {

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// Run the 2-round handshake to completion. Returns the server's verdict; writes the client's via out.
Pairing::Verdict handshake(Pairing& server, Pairing& client,
                           std::optional<PublicKey> serverPins, std::optional<PublicKey> clientPins,
                           Pairing::Verdict& clientVerdict) {
  auto sHello = server.Hello();
  auto cHello = client.Hello();
  EXPECT_TRUE(server.ConsumePeerHello(cHello));
  EXPECT_TRUE(client.ConsumePeerHello(sHello));
  auto sConfirm = server.Confirm();
  auto cConfirm = client.Confirm();
  auto sv = server.ConsumePeerConfirm(cConfirm, serverPins);  // host verifies the iPad
  clientVerdict = client.ConsumePeerConfirm(sConfirm, clientPins);  // iPad verifies the host
  return sv;
}

struct CryptoEnv : ::testing::Environment {
  void SetUp() override { ASSERT_TRUE(InitCrypto()); }
};
const auto* kEnv = ::testing::AddGlobalTestEnvironment(new CryptoEnv);

}  // namespace

TEST(Pairing, FirstPairingThenEncryptedBothDirections) {
  auto host = GenerateIdentity(), ipad = GenerateIdentity();
  Pairing server(host, Pairing::Role::Server), client(ipad, Pairing::Role::Client);

  Pairing::Verdict cv;
  EXPECT_EQ(handshake(server, client, std::nullopt, std::nullopt, cv), Pairing::Verdict::Ok);  // TOFU
  EXPECT_EQ(cv, Pairing::Verdict::Ok);
  EXPECT_EQ(server.PeerIdentity(), ipad.sign_pk);  // host learns the iPad's identity to pin
  EXPECT_EQ(client.PeerIdentity(), host.sign_pk);

  auto sc = server.MakeChannel();
  auto cc = client.MakeChannel();
  ASSERT_TRUE(cc.InitRx(sc.TxHeader()));
  ASSERT_TRUE(sc.InitRx(cc.TxHeader()));

  // host -> iPad (video)
  auto ct = sc.Encrypt(bytes("video-frame-bytes"));
  EXPECT_EQ(ct.size(), bytes("video-frame-bytes").size() + crypto_secretstream_xchacha20poly1305_ABYTES);
  auto pt = cc.Decrypt(ct);
  ASSERT_TRUE(pt);
  EXPECT_EQ(*pt, bytes("video-frame-bytes"));

  // iPad -> host (control)
  auto ct2 = cc.Encrypt(bytes("keyframe-request"));
  auto pt2 = sc.Decrypt(ct2);
  ASSERT_TRUE(pt2);
  EXPECT_EQ(*pt2, bytes("keyframe-request"));
}

TEST(Pairing, TamperedRecordRejected) {
  auto host = GenerateIdentity(), ipad = GenerateIdentity();
  Pairing server(host, Pairing::Role::Server), client(ipad, Pairing::Role::Client);
  Pairing::Verdict cv;
  ASSERT_EQ(handshake(server, client, std::nullopt, std::nullopt, cv), Pairing::Verdict::Ok);
  auto sc = server.MakeChannel();
  auto cc = client.MakeChannel();
  ASSERT_TRUE(cc.InitRx(sc.TxHeader()));

  auto ct = sc.Encrypt(bytes("secret pixels"));
  ct[ct.size() / 2] ^= 0xFF;  // flip a byte
  EXPECT_FALSE(cc.Decrypt(ct));  // Poly1305 tag fails
}

TEST(Pairing, UnknownDeviceRefused) {
  // The host has already paired with a known iPad and pins its identity. A DIFFERENT iPad connects.
  auto host = GenerateIdentity();
  auto knownIpad = GenerateIdentity();
  auto strangerIpad = GenerateIdentity();

  Pairing server(host, Pairing::Role::Server);
  Pairing stranger(strangerIpad, Pairing::Role::Client);
  Pairing::Verdict cv;
  // Host pins the KNOWN iPad; the stranger presents its own identity -> refused.
  EXPECT_EQ(handshake(server, stranger, /*serverPins=*/knownIpad.sign_pk, std::nullopt, cv),
            Pairing::Verdict::IdentityMismatch);
}

TEST(Pairing, ForgedSignatureRejected_MitmWithoutKey) {
  // A MITM presents the REAL host's identity public key (so pinning passes) but cannot sign the fresh
  // transcript with the host's secret key — it signs with its own. Signature verification must fail.
  auto host = GenerateIdentity(), ipad = GenerateIdentity(), attacker = GenerateIdentity();
  Pairing client(ipad, Pairing::Role::Client);

  // Build a malicious "server" Pairing that carries the host's identity public key but the attacker's
  // secret key. We splice an Identity with mismatched pk/sk to model the forgery.
  Identity spoof;
  spoof.sign_pk = host.sign_pk;       // claims to be the host
  spoof.sign_sk = attacker.sign_sk;   // but only holds the attacker's secret key
  Pairing fakeServer(spoof, Pairing::Role::Server);

  Pairing::Verdict cv;
  // Client pins the real host's pk; identity matches (spoofed pk) but the signature is the attacker's.
  handshake(fakeServer, client, std::nullopt, /*clientPins=*/host.sign_pk, cv);
  EXPECT_EQ(cv, Pairing::Verdict::BadSignature);
}

TEST(Pairing, ReconnectUsesFreshKeys) {
  // Same two identities pair twice; the per-connection ephemeral exchange must yield different keys,
  // so encrypting identical plaintext gives different ciphertext (forward secrecy / anti-replay).
  auto host = GenerateIdentity(), ipad = GenerateIdentity();

  auto encryptFirstRecord = [&]() {
    Pairing server(host, Pairing::Role::Server), client(ipad, Pairing::Role::Client);
    Pairing::Verdict cv;
    EXPECT_EQ(handshake(server, client, std::nullopt, std::nullopt, cv), Pairing::Verdict::Ok);
    auto sc = server.MakeChannel();
    return sc.Encrypt(bytes("identical-plaintext"));
  };
  EXPECT_NE(encryptFirstRecord(), encryptFirstRecord());
}

TEST(Pairing, DeterministicIdentityFromSeed) {
  std::vector<std::uint8_t> seed(32, 0xAB);
  auto a = IdentityFromSeed(seed);
  auto b = IdentityFromSeed(seed);
  EXPECT_EQ(a.sign_pk, b.sign_pk);  // same seed -> same identity (for stored-key recovery)
}
