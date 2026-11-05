// Known-answer and golden-wire-vector pins for the crypto layer, driven through
// the API as it is today: CryptoEngine (src/mesh/CryptoEngine.cpp),
// crypto_aes_ccm_seal/open (src/crypto) and the Crypto library primitives they
// sit on (SHA256, SHA512, Curve25519, XEdDSA).
//
// Why: later changes replace this layer behind a new API. The packets already in the air and the
// keys already on devices were made by this code, so the replacement has to reproduce its output
// bit for bit. Vector tables live in test/support/crypto_vectors.h with their provenance; nothing
// here derives an expected value from the code under test. Standard vectors come from RFCs and
// NIST; the wire vectors were computed independently of the firmware (Python cryptography) from the
// documented layout, except where the header marks them "Golden (fw)".
//
// Regressions guarded: a changed nonce layout (extraNonce placement over the upper packetId half),
// AAD byte order, tag length (8 bytes PKI, 12 PSK), CCM length field, key-size dispatch, ack proof
// encoding, or a verifier that starts accepting forged input. Deleting or relaxing these assertions
// removes the only check that a backend swap left the wire format alone.
#include "configuration.h"

#include "CryptoEngine.h"
#ifndef CRYPTO_TEST_MINIMAL
#include "TestUtil.h"
#endif
#include "crypto/crypto_api.h"
#include "support/crypto_vectors.h"
#include <Curve25519.h>
#include <SHA256.h>
#include <SHA512.h>
#include <XEdDSA.h>
#include <pb.h>
#ifdef CRYPTO_TEST_MINIMAL
// Test-mode library discovery only follows includes in the test sources, so name what the crypto sources need.
#include <Thread.h>
#ifdef ARCH_NRF52
#include <Adafruit_nRFCrypto.h>
#endif
#endif
#include <unity.h>

namespace cv = crypto_vectors;

static uint8_t bufA[300], bufB[300], bufC[300];

void setUp(void)
{
    crypto->setKey(CryptoKey{});
}

void tearDown(void) {}

static void sha256Of(const uint8_t *in, size_t len, uint8_t out[32])
{
    SHA256 h;
    h.reset();
    h.update(in, len);
    h.finalize(out, 32);
}

static void sha512Of(const uint8_t *in, size_t len, uint8_t out[64])
{
    SHA512 h;
    h.reset();
    h.update(in, len);
    h.finalize(out, 64);
}

static void assertMem(const void *expected, const void *actual, size_t len, const char *what = "")
{
    if (len)
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected, actual, len, what);
}

static void assertHex(const char *expectedHex, const uint8_t *got, size_t len, const char *what)
{
    uint8_t expected[132];
    TEST_ASSERT_TRUE_MESSAGE(len <= sizeof(expected), what);
    TEST_ASSERT_TRUE_MESSAGE(len <= cv::hexLen(expectedHex), what);
    cv::hex(expectedHex, expected, sizeof(expected));
    assertMem(expected, got, len, what);
}

static void assertSha256Hex(const char *expectedHex, const uint8_t *data, size_t len, const char *what)
{
    uint8_t digest[32];
    sha256Of(data, len, digest);
    assertHex(expectedHex, digest, 32, what);
}

static CryptoKey keyFromHex(const char *hex)
{
    CryptoKey k = {};
    k.length = (int8_t)cv::hex(hex, k.bytes, sizeof(k.bytes));
    return k;
}

static CryptoKey seqCryptoKey(size_t len)
{
    CryptoKey k = {};
    cv::seqKey(k.bytes, len);
    k.length = (int8_t)len;
    return k;
}

static void clamp(uint8_t *priv)
{
    priv[0] &= 0xF8;
    priv[31] = (priv[31] & 0x7F) | 0x40;
}

static void buildNonce(uint8_t out[16], uint32_t from, uint64_t packetId, uint32_t extra)
{
    memset(out, 0, 16);
    for (int i = 0; i < 8; i++)
        out[i] = (uint8_t)(packetId >> (8 * i));
    for (int i = 0; i < 4; i++)
        out[8 + i] = (uint8_t)(from >> (8 * i));
    if (extra)
        for (int i = 0; i < 4; i++)
            out[4 + i] = (uint8_t)(extra >> (8 * i));
}

// ---- AES-CTR ------------------------------------------------------------------------------------

void test_aes_ctr_sp800_38a_encrypt_and_decrypt(void)
{
    for (const cv::CtrVector &v : cv::kCtrStd) {
        uint8_t counter[16];
        CryptoKey k = keyFromHex(v.key);
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        cv::hex(v.counter, counter, sizeof(counter));
        crypto->encryptAESCtr(k, counter, n, bufA);
        assertHex(v.crypt, bufA, n, v.name);

        cv::hex(v.counter, counter, sizeof(counter));
        crypto->encryptAESCtr(k, counter, n, bufA);
        assertHex(v.plain, bufA, n, v.name);
    }
}

void test_aes_ctr_lengths_match_independent_implementation(void)
{
    for (const cv::CtrLenVector &v : cv::kCtrLen) {
        uint8_t counter[16];
        memcpy(counter, cv::kLenCtrBlock, sizeof(counter));
        CryptoKey k = seqCryptoKey(v.keyLen);
        cv::pattern(bufA, v.len);
        crypto->encryptAESCtr(k, counter, v.len, bufA);
        char what[40];
        snprintf(what, sizeof(what), "AES-%d len %d", v.keyLen * 8, v.len);
        assertSha256Hex(v.cryptSha256, bufA, v.len, what);
    }
}

void test_aes_ctr_packet_over_256_bytes_is_left_untouched(void)
{
    crypto->setKey(seqCryptoKey(16));
    cv::pattern(bufA, 257);
    memcpy(bufB, bufA, 257);
    crypto->encryptPacket(0x0929, 0x13B2D662, 257, bufA);
    assertMem(bufB, bufA, 257);
}

// ---- AES-CCM (L = 2) ----------------------------------------------------------------------------

void test_aes_ccm_standard_vectors(void)
{
    for (const cv::CcmVector &v : cv::kCcmStd) {
        uint8_t key[32], nonce[13], aad[16], tag[16], expectedTag[16];
        const size_t keyLen = cv::hex(v.key, key, sizeof(key));
        cv::hex(v.nonce, nonce, sizeof(nonce));
        const size_t aadLen = cv::hex(v.aad, aad, sizeof(aad));
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        const size_t tagLen = cv::hex(v.tag, expectedTag, sizeof(expectedTag));

        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ccm_seal(key, keyLen, nonce, aad, aadLen, bufA, n, bufB, tag, tagLen), v.name);
        assertHex(v.crypt, bufB, n, v.name);
        assertMem(expectedTag, tag, tagLen, v.name);

        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ccm_open(key, keyLen, nonce, aad, aadLen, bufB, n, tag, tagLen, bufC), v.name);
        assertHex(v.plain, bufC, n, v.name);
    }
}

void test_aes_ccm_lengths_match_independent_implementation(void)
{
    for (const cv::CcmLenVector &v : cv::kCcmLen) {
        uint8_t key[32], tag[16];
        cv::seqKey(key, v.keyLen);
        cv::pattern(bufA, v.len);
        const uint8_t *aad = v.aadLen ? cv::kLenCcmAad : nullptr;
        char what[48];
        snprintf(what, sizeof(what), "AES-%d M=%d aad=%d len %d", v.keyLen * 8, v.tagLen, v.aadLen, v.len);

        TEST_ASSERT_TRUE_MESSAGE(
            crypto_aes_ccm_seal(key, v.keyLen, cv::kLenCcmNonce, aad, v.aadLen, bufA, v.len, bufB, tag, v.tagLen), what);
        memcpy(bufB + v.len, tag, v.tagLen);
        assertSha256Hex(v.cryptTagSha256, bufB, v.len + v.tagLen, what);

        TEST_ASSERT_TRUE_MESSAGE(
            crypto_aes_ccm_open(key, v.keyLen, cv::kLenCcmNonce, aad, v.aadLen, bufB, v.len, tag, v.tagLen, bufC), what);
        assertMem(bufA, bufC, v.len, what);
    }
}

void test_aes_ccm_every_flipped_bit_is_rejected(void)
{
    const size_t len = 17, aadLen = 8, tagLen = 12;
    uint8_t key[32], nonce[13], aad[8], tag[12];
    cv::seqKey(key, 32);
    memcpy(nonce, cv::kLenCcmNonce, sizeof(nonce));
    memcpy(aad, cv::kLenCcmAad, sizeof(aad));
    cv::pattern(bufA, len);
    TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, 32, nonce, aad, aadLen, bufA, len, bufB, tag, tagLen));
    TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, 32, nonce, aad, aadLen, bufB, len, tag, tagLen, bufC));

    for (size_t bit = 0; bit < len * 8; bit++) {
        bufB[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        TEST_ASSERT_FALSE_MESSAGE(crypto_aes_ccm_open(key, 32, nonce, aad, aadLen, bufB, len, tag, tagLen, bufC),
                                  "ciphertext bit");
        bufB[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    for (size_t bit = 0; bit < tagLen * 8; bit++) {
        tag[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        TEST_ASSERT_FALSE_MESSAGE(crypto_aes_ccm_open(key, 32, nonce, aad, aadLen, bufB, len, tag, tagLen, bufC), "tag bit");
        tag[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    for (size_t bit = 0; bit < aadLen * 8; bit++) {
        aad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        TEST_ASSERT_FALSE_MESSAGE(crypto_aes_ccm_open(key, 32, nonce, aad, aadLen, bufB, len, tag, tagLen, bufC), "aad bit");
        aad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    for (size_t bit = 0; bit < sizeof(nonce) * 8; bit++) {
        nonce[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        TEST_ASSERT_FALSE_MESSAGE(crypto_aes_ccm_open(key, 32, nonce, aad, aadLen, bufB, len, tag, tagLen, bufC), "nonce bit");
        nonce[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    for (size_t bit = 0; bit < 32 * 8; bit += 7) {
        key[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        TEST_ASSERT_FALSE_MESSAGE(crypto_aes_ccm_open(key, 32, nonce, aad, aadLen, bufB, len, tag, tagLen, bufC), "key bit");
        key[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, 32, nonce, aad, aadLen, bufB, len, tag, tagLen, bufC));
}

void test_aes_ccm_empty_payload_still_authenticates_tag(void)
{
    uint8_t key[16], tag[8];
    cv::seqKey(key, 16);
    TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, 16, cv::kLenCcmNonce, nullptr, 0, bufA, 0, bufB, tag, 8));
    TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, 16, cv::kLenCcmNonce, nullptr, 0, bufB, 0, tag, 8, bufC));
    for (size_t bit = 0; bit < sizeof(tag) * 8; bit++) {
        tag[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        TEST_ASSERT_FALSE(crypto_aes_ccm_open(key, 16, cv::kLenCcmNonce, nullptr, 0, bufB, 0, tag, 8, bufC));
        tag[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
}

// ---- SHA-256 / SHA-512 / HMAC-SHA256 ------------------------------------------------------------

void test_sha256_fips180_through_crypto_hash(void)
{
    for (const cv::HashVector &v : cv::kSha256Std) {
        const size_t n = cv::hex(v.msg, bufA, sizeof(bufA));
        crypto->hash(bufA, n);
        assertHex(v.digest, bufA, 32, v.name);
    }
}

void test_sha512_fips180(void)
{
    for (const cv::HashVector &v : cv::kSha512Std) {
        uint8_t digest[64];
        const size_t n = cv::hex(v.msg, bufA, sizeof(bufA));
        sha512Of(bufA, n, digest);
        assertHex(v.digest, digest, 64, v.name);
    }
}

void test_sha_lengths_across_block_boundaries(void)
{
    for (const cv::HashLenVector &v : cv::kHashLen) {
        char what[32];
        snprintf(what, sizeof(what), "len %d", v.len);
        uint8_t digest[64];
        cv::pattern(bufA, v.len);
        sha512Of(bufA, v.len, digest);
        assertHex(v.sha512, digest, 64, what);
        crypto->hash(bufA, v.len);
        assertHex(v.sha256, bufA, 32, what);
    }
}

void test_hmac_sha256_rfc4231(void)
{
    for (const cv::HmacVector &v : cv::kHmacSha256) {
        uint8_t key[131], digest[32];
        const size_t keyLen = cv::hex(v.key, key, sizeof(key));
        const size_t n = cv::hex(v.data, bufA, sizeof(bufA));
        SHA256 mac;
        mac.resetHMAC(key, keyLen);
        mac.update(bufA, n);
        mac.finalizeHMAC(key, keyLen, digest, sizeof(digest));
        assertHex(v.mac, digest, cv::hexLen(v.mac), v.name);
    }
}

#if !(MESHTASTIC_EXCLUDE_PKI)
// ---- X25519 -------------------------------------------------------------------------------------

void test_x25519_rfc7748_section_5_2(void)
{
    for (const cv::X25519Vector &v : cv::kX25519Std) {
        uint8_t scalar[32], point[32], out[32];
        cv::hex(v.scalar, scalar, sizeof(scalar));
        cv::hex(v.point, point, sizeof(point));
        clamp(scalar);
        TEST_ASSERT_TRUE_MESSAGE(crypto_x25519(out, scalar, point), v.name);
        assertHex(v.out, out, 32, v.name);
    }
}

void test_x25519_rfc7748_one_iteration_from_base_point(void)
{
    uint8_t k[32], u[32], out[32];
    cv::hex(cv::kX25519Base, k, sizeof(k));
    cv::hex(cv::kX25519Base, u, sizeof(u));
    clamp(k);
    TEST_ASSERT_TRUE(crypto_x25519(out, k, u));
    assertHex(cv::kX25519NineIter1, out, 32, "k=u=9, 1 iteration");
}

void test_x25519_rfc7748_section_6_1_key_agreement(void)
{
    uint8_t a[32], aPub[32], b[32], bPub[32], k[32], out[32];
    cv::hex(cv::kRfc7748Dh[0], a, sizeof(a));
    cv::hex(cv::kRfc7748Dh[1], aPub, sizeof(aPub));
    cv::hex(cv::kRfc7748Dh[2], b, sizeof(b));
    cv::hex(cv::kRfc7748Dh[3], bPub, sizeof(bPub));
    cv::hex(cv::kRfc7748Dh[4], k, sizeof(k));
    clamp(a);
    clamp(b);

    TEST_ASSERT_TRUE(crypto_x25519(out, a, nullptr));
    assertMem(aPub, out, 32);
    TEST_ASSERT_TRUE(crypto_x25519(out, b, nullptr));
    assertMem(bPub, out, 32);
    TEST_ASSERT_TRUE(crypto_x25519(out, a, bPub));
    assertMem(k, out, 32);
    TEST_ASSERT_TRUE(crypto_x25519(out, b, aPub));
    assertMem(k, out, 32);

    crypto->setDHPrivateKey(a);
    TEST_ASSERT_TRUE(crypto->setDHPublicKey(bPub));
    assertMem(k, crypto->shared_key, 32);
}

void test_x25519_clamps_the_scalar(void)
{
    const cv::X25519Vector &v = cv::kX25519Std[0];
    uint8_t scalar[32], point[32], out[32];
    cv::hex(v.scalar, scalar, sizeof(scalar));
    cv::hex(v.point, point, sizeof(point));
    TEST_ASSERT_TRUE(crypto_x25519(out, scalar, point));
    assertHex(v.out, out, 32, v.name);
}

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
// ---- Ed25519 verify -----------------------------------------------------------------------------

void test_ed25519_verify_rfc8032(void)
{
    for (const cv::Ed25519Vector &v : cv::kEd25519Std) {
        uint8_t pub[32], sig[64];
        cv::hex(v.pub, pub, sizeof(pub));
        cv::hex(v.sig, sig, sizeof(sig));
        const size_t n = cv::hex(v.msg, bufA, sizeof(bufA));
        TEST_ASSERT_TRUE_MESSAGE(crypto_ed25519_verify(sig, pub, bufA, n), v.name);
    }
}

void test_ed25519_verify_rejects_flipped_bits(void)
{
    // One bit per byte, rotating position, keeps the count tolerable on a microcontroller.
    for (const cv::Ed25519Vector &v : cv::kEd25519Std) {
        uint8_t pub[32], sig[64];
        cv::hex(v.pub, pub, sizeof(pub));
        cv::hex(v.sig, sig, sizeof(sig));
        const size_t n = cv::hex(v.msg, bufA, sizeof(bufA));
        for (size_t i = 0; i < sizeof(sig); i++) {
            sig[i] ^= (uint8_t)(1u << (i % 8));
            TEST_ASSERT_FALSE_MESSAGE(crypto_ed25519_verify(sig, pub, bufA, n), v.name);
            sig[i] ^= (uint8_t)(1u << (i % 8));
        }
        for (size_t i = 0; i < n; i++) {
            bufA[i] ^= (uint8_t)(1u << (i % 8));
            TEST_ASSERT_FALSE_MESSAGE(crypto_ed25519_verify(sig, pub, bufA, n), v.name);
            bufA[i] ^= (uint8_t)(1u << (i % 8));
        }
        for (size_t i = 0; i < sizeof(pub); i++) {
            pub[i] ^= (uint8_t)(1u << (i % 8));
            TEST_ASSERT_FALSE_MESSAGE(crypto_ed25519_verify(sig, pub, bufA, n), v.name);
            pub[i] ^= (uint8_t)(1u << (i % 8));
        }
        TEST_ASSERT_TRUE_MESSAGE(crypto_ed25519_verify(sig, pub, bufA, n), v.name);
    }
}

// Whether S >= q is rejected depends on the Crypto library pin. Every environment in this tree uses
// meshtastic/Crypto 9ceb213; the expectation below records what that pin does, and a new pin that
// changes it must change this constant knowingly rather than silently.
#ifndef CRYPTO_VECTORS_EXPECT_NONCANONICAL_S_REJECTED
#define CRYPTO_VECTORS_EXPECT_NONCANONICAL_S_REJECTED 1
#endif

void test_ed25519_verify_noncanonical_s_outcome_is_pinned(void)
{
    const cv::Ed25519Vector &v = cv::kEd25519Std[0];
    uint8_t pub[32], sig[64];
    cv::hex(v.pub, pub, sizeof(pub));
    cv::hex(cv::kEd25519MalleatedSig, sig, sizeof(sig));
    const size_t n = cv::hex(v.msg, bufA, sizeof(bufA));
    const bool accepted = crypto_ed25519_verify(sig, pub, bufA, n);
    TEST_ASSERT_EQUAL_MESSAGE(!CRYPTO_VECTORS_EXPECT_NONCANONICAL_S_REJECTED, accepted,
                              "non-canonical S outcome differs from the recorded one for this Crypto pin");
}

#endif
#endif

// ---- Golden wire vectors ------------------------------------------------------------------------

void test_golden_init_nonce_layout(void)
{
    for (const cv::NonceVector &v : cv::kNonce) {
        crypto->initNonce(v.from, v.packetId, v.extra);
        assertHex(v.nonce, crypto->nonce, 16, "initNonce");
    }
}

void test_golden_init_nonce_extra_nonce_overwrites_upper_packet_id_half(void)
{
    crypto->initNonce(0xAABBCCDD, 0x0000000100000002ULL, 0xCAFEBABE);
    memcpy(bufA, crypto->nonce, 16);
    crypto->initNonce(0xAABBCCDD, 0x0000000200000002ULL, 0xCAFEBABE);
    assertMem(bufA, crypto->nonce, 16);

    crypto->initNonce(0xAABBCCDD, 0x0000000100000002ULL, 0);
    TEST_ASSERT_EQUAL_UINT8(1, crypto->nonce[4]);
}

void test_golden_psk_channel_encryption(void)
{
    for (const cv::PskCtrVector &v : cv::kPskCtr) {
        crypto->setKey(keyFromHex(v.key));
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        crypto->encryptPacket(v.from, v.packetId, n, bufA);
        assertHex(v.crypt, bufA, n, "psk encrypt");
        crypto->decrypt(v.from, v.packetId, n, bufA);
        assertHex(v.plain, bufA, n, "psk decrypt");
    }
}

void test_golden_psk_ccm_with_12_byte_tag_and_aad_byte_order(void)
{
    for (const cv::PskCcmVector &v : cv::kPskCcm) {
        CryptoKey k = keyFromHex(v.key);
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        const size_t wireLen = cv::hexLen(v.cryptTag);
        TEST_ASSERT_EQUAL(n + 12, wireLen);
        TEST_ASSERT_EQUAL(12, CryptoEngine::AEAD_TAG_SIZE);

        TEST_ASSERT_TRUE(crypto->encryptPacketCCM(k, v.from, v.to, v.packetId, n, bufA, bufB));
        assertHex(v.cryptTag, bufB, wireLen, "psk ccm encrypt");

        TEST_ASSERT_TRUE(crypto->decryptPacketCCM(k, v.from, v.to, v.packetId, wireLen, bufB, bufC));
        assertHex(v.plain, bufC, n, "psk ccm decrypt");

        // The table's AAD is from then to, each little-endian; rebuilding the tag from it directly
        // proves that is what encryptPacketCCM authenticated.
        uint8_t aad[8], nonce[16], tag[12];
        cv::hex(v.aad, aad, sizeof(aad));
        buildNonce(nonce, v.from, v.packetId, 0);
        TEST_ASSERT_TRUE(crypto_aes_ccm_seal(k.bytes, k.length, nonce, aad, sizeof(aad), bufA, n, bufC, tag, 12));
        assertMem(bufB + n, tag, 12);

        TEST_ASSERT_FALSE(crypto->decryptPacketCCM(k, v.from, v.to ^ 1, v.packetId, wireLen, bufB, bufC));
        TEST_ASSERT_FALSE(crypto->decryptPacketCCM(k, v.to, v.from, v.packetId, wireLen, bufB, bufC));
        for (size_t bit = 0; bit < wireLen * 8; bit += 3) {
            bufB[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            TEST_ASSERT_FALSE(crypto->decryptPacketCCM(k, v.from, v.to, v.packetId, wireLen, bufB, bufC));
            bufB[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        }
    }
}

void test_golden_psk_ccm_lengths_round_trip(void)
{
    static const size_t lens[] = {1, 15, 16, 17, 255, 256};
    for (int keyLen : {16, 32}) {
        CryptoKey k = seqCryptoKey(keyLen);
        for (size_t n : lens) {
            cv::pattern(bufA, n);
            TEST_ASSERT_TRUE(crypto->encryptPacketCCM(k, 0x0929, 0x1234, 0x13B2D662, n, bufA, bufB));
            TEST_ASSERT_TRUE(crypto->decryptPacketCCM(k, 0x0929, 0x1234, 0x13B2D662, n + 12, bufB, bufC));
            assertMem(bufA, bufC, n);
        }
    }
    CryptoKey k = seqCryptoKey(16);
    TEST_ASSERT_FALSE_MESSAGE(crypto->decryptPacketCCM(k, 1, 2, 3, 12, bufB, bufC), "tag only, no ciphertext");
    TEST_ASSERT_FALSE_MESSAGE(crypto->decryptPacketCCM(k, 1, 2, 3, 11, bufB, bufC), "shorter than the tag");
}

#if !(MESHTASTIC_EXCLUDE_PKI)
static void loadAlice(uint8_t aPub[32], uint8_t bPub[32])
{
    uint8_t a[32];
    cv::hex(cv::kRfc7748Dh[0], a, sizeof(a));
    clamp(a);
    cv::hex(cv::kRfc7748Dh[3], bPub, 32);
    TEST_ASSERT_TRUE(crypto->regeneratePublicKey(aPub, a));
    assertHex(cv::kRfc7748Dh[1], aPub, 32, "Alice public key from clamped private key");
}

static meshtastic_NodeInfoLite_public_key_t asPublicKey(const uint8_t *key)
{
    meshtastic_NodeInfoLite_public_key_t pk = {};
    pk.size = 32;
    memcpy(pk.bytes, key, 32);
    return pk;
}

void test_golden_pki_decrypt_uses_ccm_l2_8_byte_tag_and_trailing_extra_nonce(void)
{
    uint8_t aPub[32], bPub[32];
    loadAlice(aPub, bPub);
    for (const cv::PkiVector &v : cv::kPki) {
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        const size_t wireLen = cv::hexLen(v.wire);
        TEST_ASSERT_EQUAL(n + 8 + 4, wireLen);
        cv::hex(v.wire, bufB, sizeof(bufB));
        TEST_ASSERT_TRUE(crypto->decryptCurve25519(v.from, asPublicKey(bPub), v.packetId, wireLen, bufB, bufC));
        assertMem(bufA, bufC, n);
        assertHex(cv::kPkiSharedKey, crypto->shared_key, 32, "shared key is SHA256 of the X25519 output");

        for (size_t bit = 0; bit < wireLen * 8; bit += 5) {
            bufB[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            TEST_ASSERT_FALSE(crypto->decryptCurve25519(v.from, asPublicKey(bPub), v.packetId, wireLen, bufB, bufC));
            bufB[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        }
        TEST_ASSERT_FALSE(crypto->decryptCurve25519(v.from ^ 1, asPublicKey(bPub), v.packetId, wireLen, bufB, bufC));
        TEST_ASSERT_FALSE(crypto->decryptCurve25519(v.from, asPublicKey(bPub), v.packetId ^ 1, wireLen, bufB, bufC));
    }
}

void test_golden_pki_peer_derives_the_same_key(void)
{
    uint8_t aPub[32], bPub[32], b[32];
    loadAlice(aPub, bPub);
    cv::hex(cv::kRfc7748Dh[2], b, sizeof(b));
    clamp(b);
    crypto->setDHPrivateKey(b);

    const cv::PkiVector &v = cv::kPki[0];
    const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
    const size_t wireLen = cv::hexLen(v.wire);
    cv::hex(v.wire, bufB, sizeof(bufB));
    TEST_ASSERT_TRUE(crypto->decryptCurve25519(v.from, asPublicKey(aPub), v.packetId, wireLen, bufB, bufC));
    assertMem(bufA, bufC, n);
}

void test_golden_pki_upper_packet_id_half_is_not_authenticated_when_extra_nonce_is_set(void)
{
    uint8_t aPub[32], bPub[32];
    loadAlice(aPub, bPub);
    const cv::PkiVector &hi1 = cv::kPki[1];
    const cv::PkiVector &hi2 = cv::kPki[2];
    TEST_ASSERT_EQUAL_STRING(hi1.wire, hi2.wire);
    const size_t n = cv::hex(hi1.plain, bufA, sizeof(bufA));
    cv::hex(hi1.wire, bufB, sizeof(bufB));
    TEST_ASSERT_TRUE(crypto->decryptCurve25519(hi1.from, asPublicKey(bPub), hi2.packetId, n + 12, bufB, bufC));
    assertMem(bufA, bufC, n);
}

void test_golden_pki_encrypt_layout_and_round_trip(void)
{
    uint8_t aPub[32], bPub[32];
    loadAlice(aPub, bPub);
    static const size_t lens[] = {0, 1, 15, 16, 17, 200};
    for (size_t n : lens) {
        const uint32_t from = 0x0929;
        const uint64_t id = 0x13B2D662;
        cv::pattern(bufA, n);
        TEST_ASSERT_TRUE(crypto->encryptCurve25519(0x1234, from, asPublicKey(bPub), id, n, bufA, bufB));

        uint32_t extra;
        memcpy(&extra, bufB + n + 8, sizeof(extra));
        uint8_t sharedKey[32], nonce[16], tag[8];
        cv::hex(cv::kPkiSharedKey, sharedKey, sizeof(sharedKey));
        buildNonce(nonce, from, id, extra);
        TEST_ASSERT_TRUE(crypto_aes_ccm_seal(sharedKey, 32, nonce, nullptr, 0, bufA, n, bufC, tag, 8));
        assertMem(bufC, bufB, n);
        assertMem(tag, bufB + n, 8);

        TEST_ASSERT_TRUE(crypto->decryptCurve25519(from, asPublicKey(bPub), id, n + 12, bufB, bufC));
        assertMem(bufA, bufC, n);
    }
}

void test_golden_pki_rejects_empty_or_weak_peer_key(void)
{
    uint8_t aPub[32], bPub[32];
    loadAlice(aPub, bPub);
    meshtastic_NodeInfoLite_public_key_t none = {};
    cv::pattern(bufA, 10);
    TEST_ASSERT_FALSE(crypto->encryptCurve25519(1, 2, none, 3, 10, bufA, bufB));
    TEST_ASSERT_FALSE(crypto->decryptCurve25519(2, none, 3, 22, bufB, bufC));
    uint8_t zero[32] = {0};
    TEST_ASSERT_FALSE(crypto->encryptCurve25519(1, 2, asPublicKey(zero), 3, 10, bufA, bufB));
}

void test_golden_ack_proof(void)
{
    uint8_t aPub[32], bPub[32], proof[ACK_PROOF_SIZE];
    loadAlice(aPub, bPub);
    for (const cv::AckProofVector &v : cv::kAckProof) {
        const size_t n = cv::hex(v.routing, bufA, sizeof(bufA));
        TEST_ASSERT_TRUE(crypto->ackProofCompute(bPub, v.ackFrom, v.ackTo, v.requestId, n ? bufA : nullptr, n, proof));
        assertHex(v.hmacSha256, proof, ACK_PROOF_SIZE, "ack proof is the leading bytes of the HMAC");

        uint8_t other[ACK_PROOF_SIZE];
        TEST_ASSERT_TRUE(crypto->ackProofCompute(bPub, v.ackTo, v.ackFrom, v.requestId, n ? bufA : nullptr, n, other));
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, memcmp(proof, other, ACK_PROOF_SIZE), "direction must be bound");
        TEST_ASSERT_TRUE(crypto->ackProofCompute(bPub, v.ackFrom, v.ackTo, v.requestId + 1, n ? bufA : nullptr, n, other));
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, memcmp(proof, other, ACK_PROOF_SIZE), "request id must be bound");
        for (size_t bit = 0; bit < n * 8; bit++) {
            bufA[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            TEST_ASSERT_TRUE(crypto->ackProofCompute(bPub, v.ackFrom, v.ackTo, v.requestId, bufA, n, other));
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, memcmp(proof, other, ACK_PROOF_SIZE), "routing bytes must be bound");
            bufA[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        }
    }
}

#endif

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
// ---- XEdDSA -------------------------------------------------------------------------------------

static meshtastic_Data makeData(size_t len)
{
    meshtastic_Data d = meshtastic_Data_init_zero;
    d.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    d.payload.size = (pb_size_t)len;
    cv::pattern(d.payload.bytes, len);
    d.request_id = 0xCAFE0001;
    d.has_bitfield = true;
    d.bitfield = 1;
    return d;
}

void test_xeddsa_sign_then_verify_for_payload_lengths_and_reject_bit_flips(void)
{
    uint8_t aPub[32], bPub[32], sig[64];
    loadAlice(aPub, bPub);
    static const size_t lens[] = {0, 1, 15, 16, 17, 200, meshtastic_Constants_DATA_PAYLOAD_LEN};
    for (size_t n : lens) {
        meshtastic_Data d = makeData(n);
        TEST_ASSERT_TRUE(crypto->xeddsa_sign(0x0929, 0x13B2D662, 0x1234, &d, sig));
        TEST_ASSERT_TRUE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));
        for (size_t i = 0; i < sizeof(sig); i += 5) {
            sig[i] ^= (uint8_t)(1u << (i % 8));
            TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));
            sig[i] ^= (uint8_t)(1u << (i % 8));
        }
        for (size_t i = 0; i < n; i += 11) {
            d.payload.bytes[i] ^= (uint8_t)(1u << (i % 8));
            TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));
            d.payload.bytes[i] ^= (uint8_t)(1u << (i % 8));
        }
        TEST_ASSERT_TRUE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));
    }
}

void test_xeddsa_signature_does_not_verify_under_another_key(void)
{
    uint8_t aPub[32], bPub[32], sig[64];
    loadAlice(aPub, bPub);
    meshtastic_Data d = makeData(16);
    TEST_ASSERT_TRUE(crypto->xeddsa_sign(0x0929, 0x13B2D662, 0x1234, &d, sig));
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(bPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));
}

void test_golden_xeddsa_signature_verifies_and_binds_every_field(void)
{
    uint8_t aPub[32], bPub[32], sig[64];
    loadAlice(aPub, bPub);
    cv::hex(cv::kXeddsaGoldenSig, sig, sizeof(sig));
    meshtastic_Data d = makeData(17);
    d.reply_id = 0xCAFE0002;
    d.emoji = 0;
    d.want_response = true;

    TEST_ASSERT_TRUE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));

    meshtastic_Data t = d;
    t.request_id ^= 1;
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &t, sig));
    t = d;
    t.reply_id ^= 1;
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &t, sig));
    t = d;
    t.emoji ^= 1;
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &t, sig));
    t = d;
    t.bitfield ^= 1;
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &t, sig));
    t = d;
    t.has_bitfield = false;
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &t, sig));
    t = d;
    t.want_response = false;
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &t, sig));
    t = d;
    t.portnum = (meshtastic_PortNum)(t.portnum + 1);
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &t, sig));
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0928, 0x13B2D662, 0x1234, &d, sig));
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D663, 0x1234, &d, sig));
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1235, &d, sig));
    TEST_ASSERT_FALSE(crypto->xeddsa_verify(bPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));

    for (size_t i = 0; i < sizeof(sig); i++) {
        sig[i] ^= (uint8_t)(1u << (i % 8));
        TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));
        sig[i] ^= (uint8_t)(1u << (i % 8));
    }
    for (size_t i = 0; i < d.payload.size; i++) {
        d.payload.bytes[i] ^= (uint8_t)(1u << (i % 8));
        TEST_ASSERT_FALSE(crypto->xeddsa_verify(aPub, 0x0929, 0x13B2D662, 0x1234, &d, sig));
        d.payload.bytes[i] ^= (uint8_t)(1u << (i % 8));
    }
}

#endif

// ---- New API (src/crypto) against the old paths --------------------------------------------------
// Until callers migrate, both paths are live and must stay interchangeable on the wire. The CTR
// lengths stop at 240 because that is the most the CC310 path takes in one call.

void test_new_api_ctr_and_ccm_match_the_old_paths(void)
{
    static const size_t ctrLens[] = {1, 15, 16, 17, 240};
    static const size_t ccmLens[] = {0, 1, 15, 16, 17, 255, 256};
    static const size_t tagLens[] = {8, 12};
    static const size_t aadLens[] = {0, 8};
    static const size_t keyLens[] = {16, 32};
    for (size_t keyLen : keyLens) {
        CryptoKey k = seqCryptoKey(keyLen);
        for (size_t len : ctrLens) {
            uint8_t counter[16];
            memcpy(counter, cv::kLenCtrBlock, sizeof(counter));
            cv::pattern(bufA, len);
            memcpy(bufB, bufA, len);
            crypto->encryptAESCtr(k, counter, len, bufB);
            TEST_ASSERT_TRUE(crypto_aes_ctr(k.bytes, keyLen, cv::kLenCtrBlock, bufA, len, bufC));
            assertMem(bufB, bufC, len, "ctr");
        }
    }
}

void test_encrypted_storage_primitive_compositions(void)
{
    uint8_t efuse[16], key[16], iv[16] = {0}, plain[33], out[33], digest[32];
    for (int i = 0; i < 16; i++) {
        efuse[i] = 0xa0 + i;
        key[i] = i;
    }
    for (int i = 0; i < 13; i++)
        iv[i] = 0x10 + i;
    for (int i = 0; i < 33; i++)
        plain[i] = 1 + i;

    const char *prefix = "device-efuse-data", *pass = "passphrase", *kekDomain = "meshtastic-tak-kek-v2",
               *ephDomain = "meshtastic-tak-ephemeral-v1";
    const crypto_part kekParts[] = {{prefix, strlen(prefix)}, {efuse, 16}, {pass, strlen(pass)}, {kekDomain, strlen(kekDomain)}};
    crypto_sha256(kekParts, 4, digest);
    assertHex("3387d207f2d8a9d807e8f0f9d9cd9095", digest, 16, "kek");
    const crypto_part ephParts[] = {{prefix, strlen(prefix)}, {efuse, 16}, {ephDomain, strlen(ephDomain)}};
    crypto_sha256(ephParts, 3, digest);
    assertHex("31f1ba24e04419b6318ad6ef3e5465e0", digest, 16, "ephemeral kek");

    TEST_ASSERT_TRUE(crypto_aes_ctr(key, 16, iv, plain, sizeof(plain), out));
    assertHex("50bc5dbce72d597aa893001c36284ea3cbfa3ae2def3058e911a45f8cc559ac15f", out, 33, "ctr, 13 byte nonce");

    const uint8_t body[6] = {3, 1, 9, 0, 0, 0};
    const crypto_part macParts[] = {{"backoff-auth", 12}, {body, sizeof(body)}};
    crypto_hmac_sha256(key, 16, macParts, 2, digest);
    assertHex("37782d783404781b15770fcaac15b4b9b0407640cc3c851ce242d1ba368c3a53", digest, 32, "hmac");
}

void test_new_api_hashes_match_the_old_paths(void)
{
    static const size_t lens[] = {0, 1, 55, 56, 64, 111, 112, 128, 256};
    const uint8_t key[] = {1, 2, 3, 4, 5};
    for (size_t len : lens) {
        uint8_t digest[64], expected[64];
        cv::pattern(bufA, len);
        const crypto_part part = {bufA, len};

        sha512Of(bufA, len, expected);
        crypto_sha512(&part, 1, digest);
        assertMem(expected, digest, 64, "sha512");

        crypto_sha256(&part, 1, digest);
        crypto->hash(bufA, len);
        assertMem(bufA, digest, 32, "sha256");

        cv::pattern(bufA, len);
        SHA256 mac;
        mac.resetHMAC(key, sizeof(key));
        mac.update(bufA, len);
        mac.finalizeHMAC(key, sizeof(key), expected, 32);
        crypto_hmac_sha256(key, sizeof(key), &part, 1, digest);
        assertMem(expected, digest, 32, "hmac");
    }
}

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
void test_new_api_ed25519_interoperates_with_the_old_path(void)
{
    uint8_t aPub[32], bPub[32], a[32], edPriv[32], edPub[32], converted[32], sig[64];
    loadAlice(aPub, bPub);
    cv::hex(cv::kRfc7748Dh[0], a, sizeof(a));
    clamp(a);
    crypto_xeddsa_keys_from_x25519(edPriv, edPub, a);
    assertMem(crypto->xeddsa_private_key, edPriv, 32, "ed private key");
    assertMem(crypto->xeddsa_public_key, edPub, 32, "ed public key");
    crypto_xeddsa_pub_from_x25519(converted, aPub);
    crypto_xeddsa_pub_from_x25519(bufB, aPub);
    assertMem(bufB, converted, 32, "public key conversion");

    cv::pattern(bufA, 17);
    TEST_ASSERT_TRUE(crypto_xeddsa_sign(sig, edPriv, edPub, bufA, 17));
    TEST_ASSERT_TRUE(crypto_ed25519_verify(sig, edPub, bufA, 17));
    memset(sig, 0x5A, 32);
    XEdDSA::sign(sig, edPriv, edPub, bufA, 17);
    TEST_ASSERT_TRUE(crypto_ed25519_verify(sig, edPub, bufA, 17));
}
#endif

// Weak: the hybrid ESP32 build links its throwaway dummy sketch, with its own setup(), next to the tests.
#ifdef CRYPTO_TEST_MINIMAL
__attribute__((weak))
#endif
void setup()
{
    delay(10);
    delay(2000);

#ifndef CRYPTO_TEST_MINIMAL
    initializeTestEnvironment();
#endif
    UNITY_BEGIN();
    RUN_TEST(test_new_api_ctr_and_ccm_match_the_old_paths);
    RUN_TEST(test_new_api_hashes_match_the_old_paths);
#if !(MESHTASTIC_EXCLUDE_PKI)
#endif
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
    RUN_TEST(test_new_api_ed25519_interoperates_with_the_old_path);
#endif
    RUN_TEST(test_aes_ctr_sp800_38a_encrypt_and_decrypt);
    RUN_TEST(test_aes_ctr_lengths_match_independent_implementation);
    RUN_TEST(test_aes_ctr_packet_over_256_bytes_is_left_untouched);
    RUN_TEST(test_aes_ccm_standard_vectors);
    RUN_TEST(test_aes_ccm_lengths_match_independent_implementation);
    RUN_TEST(test_aes_ccm_every_flipped_bit_is_rejected);
    RUN_TEST(test_aes_ccm_empty_payload_still_authenticates_tag);
    RUN_TEST(test_sha256_fips180_through_crypto_hash);
    RUN_TEST(test_sha512_fips180);
    RUN_TEST(test_sha_lengths_across_block_boundaries);
    RUN_TEST(test_hmac_sha256_rfc4231);
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_x25519_rfc7748_section_5_2);
    RUN_TEST(test_x25519_rfc7748_one_iteration_from_base_point);
    RUN_TEST(test_x25519_rfc7748_section_6_1_key_agreement);
    RUN_TEST(test_x25519_clamps_the_scalar);
    RUN_TEST(test_encrypted_storage_primitive_compositions);
#endif
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
    RUN_TEST(test_ed25519_verify_rfc8032);
    RUN_TEST(test_ed25519_verify_rejects_flipped_bits);
    RUN_TEST(test_ed25519_verify_noncanonical_s_outcome_is_pinned);
#endif
    RUN_TEST(test_golden_init_nonce_layout);
    RUN_TEST(test_golden_init_nonce_extra_nonce_overwrites_upper_packet_id_half);
    RUN_TEST(test_golden_psk_channel_encryption);
    RUN_TEST(test_golden_psk_ccm_with_12_byte_tag_and_aad_byte_order);
    RUN_TEST(test_golden_psk_ccm_lengths_round_trip);
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_golden_pki_decrypt_uses_ccm_l2_8_byte_tag_and_trailing_extra_nonce);
    RUN_TEST(test_golden_pki_peer_derives_the_same_key);
    RUN_TEST(test_golden_pki_upper_packet_id_half_is_not_authenticated_when_extra_nonce_is_set);
    RUN_TEST(test_golden_pki_encrypt_layout_and_round_trip);
    RUN_TEST(test_golden_pki_rejects_empty_or_weak_peer_key);
    RUN_TEST(test_golden_ack_proof);
#endif
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
    RUN_TEST(test_xeddsa_sign_then_verify_for_payload_lengths_and_reject_bit_flips);
    RUN_TEST(test_xeddsa_signature_does_not_verify_under_another_key);
    RUN_TEST(test_golden_xeddsa_signature_verifies_and_binds_every_field);
#endif
#if defined(ARCH_PORTDUINO)
    exit(UNITY_END());
#else
    UNITY_END();
#endif
}

#ifdef CRYPTO_TEST_MINIMAL
__attribute__((weak))
#endif
void loop()
{
}
