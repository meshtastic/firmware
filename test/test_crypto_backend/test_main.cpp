// Known-answer, wire-format, tamper and buffer-handling tests for the functional crypto API in
// src/crypto/crypto_api.h: crypto_aes_ctr, crypto_aes_ccm_seal/open, crypto_sha256/512,
// crypto_hmac_sha256, crypto_x25519, crypto_ed25519_verify, crypto_ed25519_basemul and
// crypto_xeddsa_sign, on whichever backend the target selects.
//
// Why: CryptoEngine moves onto this API and then its backends get replaced, one at a time, across
// boards. The packets in the air and the keys on devices must keep working, so every backend has to
// produce these exact bytes. Expected values come from test/support/crypto_vectors.h with their
// provenance; none is derived from the code under test. This source names only crypto_api.h, so it
// runs unchanged on Linux and through `pio test -e crypto-<board> -f test_crypto_backend`.
//
// Regressions guarded: a changed nonce or AAD layout, tag length or CCM length field; in-place calls
// corrupting data (the old aes_ccm_* wrote the keystream into out before reading in, so the moved
// code must not); buffers misread when unaligned or at block boundaries; forged or tampered input
// being accepted, including a malleated Ed25519 S that the shared check must reject before any
// backend sees it; and crypto_x25519 not clamping, which RFC 7748 and the design both require.
#include "configuration.h"

#include "crypto/crypto_api.h"
#ifndef CRYPTO_TEST_MINIMAL
#include "TestUtil.h"
#endif
#include "support/crypto_vectors.h"
#include <Arduino.h>
#ifdef CRYPTO_TEST_MINIMAL
// Test-mode library discovery only follows includes in the test sources, so name what the crypto sources need.
#include <Curve25519.h>
#include <SHA256.h>
#include <SHA512.h>
#include <Thread.h>
#include <XEdDSA.h>
#include <pb.h>
#ifdef ARCH_NRF52
#include <Adafruit_nRFCrypto.h>
#endif
#endif
#include <string.h>
#include <unity.h>

namespace cv = crypto_vectors;

static const size_t kLengths[] = {0, 1, 15, 16, 17, 255, 256};
static const size_t kKeyLengths[] = {16, 32};

// 3 extra bytes so a test can start a buffer at any alignment.
static uint8_t bufA[304], bufB[304], bufC[304];

void setUp(void) {}

void tearDown(void) {}

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
    const crypto_part part = {data, len};
    crypto_sha256(&part, 1, digest);
    assertHex(expectedHex, digest, 32, what);
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
        uint8_t key[32], counter[16];
        const size_t keyLen = cv::hex(v.key, key, sizeof(key));
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        cv::hex(v.counter, counter, sizeof(counter));
        const uint8_t counterBefore = counter[15];

        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ctr(key, keyLen, counter, bufA, n, bufB), v.name);
        assertHex(v.crypt, bufB, n, v.name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(counterBefore, counter[15], "the caller's counter is const");

        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ctr(key, keyLen, counter, bufB, n, bufC), v.name);
        assertHex(v.plain, bufC, n, v.name);
    }
}

void test_aes_ctr_lengths_match_independent_implementation(void)
{
    for (const cv::CtrLenVector &v : cv::kCtrLen) {
        uint8_t key[32];
        char what[32];
        snprintf(what, sizeof(what), "AES-%d len %d", v.keyLen * 8, v.len);
        cv::seqKey(key, v.keyLen);
        cv::pattern(bufA, v.len);
        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ctr(key, v.keyLen, cv::kLenCtrBlock, bufA, v.len, bufB), what);
        assertSha256Hex(v.cryptSha256, bufB, v.len, what);
    }
}

void test_aes_ctr_in_place_and_unaligned_match_disjoint_output(void)
{
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : kLengths) {
            cv::pattern(bufA, len);
            TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, bufA, len, bufB));

            memcpy(bufC, bufA, len);
            TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, bufC, len, bufC));
            assertMem(bufB, bufC, len, "in place");

            for (size_t offset = 1; offset < 4; offset++) {
                memcpy(bufC + offset, bufA, len);
                TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, bufC + offset, len, bufC + offset));
                assertMem(bufB, bufC + offset, len, "unaligned in place");
            }
        }
    }
}

void test_aes_ctr_rejects_key_sizes_other_than_128_and_256(void)
{
    uint8_t key[32] = {0};
    cv::pattern(bufA, 16);
    memcpy(bufB, bufA, 16);
    for (size_t keyLen : {(size_t)0, (size_t)1, (size_t)15, (size_t)24, (size_t)33}) {
        TEST_ASSERT_FALSE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, bufB, 16, bufB));
        assertMem(bufA, bufB, 16, "a rejected key leaves the data alone");
    }
}

// ---- AES-CCM ------------------------------------------------------------------------------------

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

void test_aes_ccm_in_place_and_unaligned_match_disjoint_output(void)
{
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : kLengths) {
            uint8_t tag[12], tagInPlace[12];
            cv::pattern(bufA, len);
            TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, cv::kLenCcmAad, 8, bufA, len, bufB, tag, 12));

            for (size_t offset = 0; offset < 4; offset++) {
                uint8_t *p = bufC + offset;
                memcpy(p, bufA, len);
                TEST_ASSERT_TRUE(
                    crypto_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, cv::kLenCcmAad, 8, p, len, p, tagInPlace, 12));
                assertMem(bufB, p, len, "seal in place");
                assertMem(tag, tagInPlace, 12, "seal in place tag");

                TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, cv::kLenCcmAad, 8, p, len, tag, 12, p));
                assertMem(bufA, p, len, "open in place");
            }
        }
    }
}

void test_aes_ccm_every_flipped_bit_is_rejected(void)
{
    const size_t len = 17;
    uint8_t key[16], nonce[13], aad[8], tag[12];
    cv::seqKey(key, sizeof(key));
    memcpy(nonce, cv::kLenCcmNonce, sizeof(nonce));
    memcpy(aad, cv::kLenCcmAad, sizeof(aad));
    cv::pattern(bufA, len);
    TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, sizeof(key), nonce, aad, sizeof(aad), bufA, len, bufB, tag, sizeof(tag)));

    auto opens = [&]() {
        return crypto_aes_ccm_open(key, sizeof(key), nonce, aad, sizeof(aad), bufB, len, tag, sizeof(tag), bufC);
    };
    auto rejectsFlips = [&](uint8_t *p, size_t n) {
        for (size_t bit = 0; bit < n * 8; bit++) {
            p[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            TEST_ASSERT_FALSE(opens());
            p[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        }
    };
    TEST_ASSERT_TRUE(opens());
    rejectsFlips(bufB, len);
    rejectsFlips(tag, sizeof(tag));
    rejectsFlips(aad, sizeof(aad));
    rejectsFlips(nonce, sizeof(nonce));
    rejectsFlips(key, sizeof(key));
    TEST_ASSERT_TRUE(opens());
}

void test_aes_ccm_empty_payload_still_authenticates(void)
{
    uint8_t key[16], tag[8];
    cv::seqKey(key, sizeof(key));
    TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, sizeof(key), cv::kLenCcmNonce, nullptr, 0, nullptr, 0, nullptr, tag, sizeof(tag)));
    TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, sizeof(key), cv::kLenCcmNonce, nullptr, 0, nullptr, 0, tag, sizeof(tag), nullptr));
    tag[0] ^= 1;
    TEST_ASSERT_FALSE(crypto_aes_ccm_open(key, sizeof(key), cv::kLenCcmNonce, nullptr, 0, nullptr, 0, tag, sizeof(tag), nullptr));
}

// The length field is 16 bits, so a longer message would silently authenticate as its truncation.
void test_aes_ccm_rejects_parameters_outside_the_format(void)
{
    uint8_t key[16] = {0}, aad[31] = {0}, tag[18];
    const uint8_t *n = cv::kLenCcmNonce;
    TEST_ASSERT_FALSE(crypto_aes_ccm_seal(key, 15, n, nullptr, 0, bufA, 1, bufB, tag, 8));
    TEST_ASSERT_FALSE(crypto_aes_ccm_seal(key, 24, n, nullptr, 0, bufA, 1, bufB, tag, 8));
    TEST_ASSERT_FALSE(crypto_aes_ccm_seal(key, 16, n, aad, 31, bufA, 1, bufB, tag, 8));
    TEST_ASSERT_FALSE(crypto_aes_ccm_seal(key, 16, n, nullptr, 0, bufA, 0x10000, bufB, tag, 8));
    TEST_ASSERT_FALSE(crypto_aes_ccm_open(key, 16, n, nullptr, 0, bufA, 0x10000, tag, 8, bufB));
    for (size_t tagLen : {(size_t)0, (size_t)2, (size_t)7, (size_t)18}) {
        TEST_ASSERT_FALSE(crypto_aes_ccm_seal(key, 16, n, nullptr, 0, bufA, 1, bufB, tag, tagLen));
        TEST_ASSERT_FALSE(crypto_aes_ccm_open(key, 16, n, nullptr, 0, bufA, 1, tag, tagLen, bufB));
    }
}

// ---- SHA and HMAC -------------------------------------------------------------------------------

void test_sha_fips180_vectors(void)
{
    uint8_t digest[64];
    for (const cv::HashVector &v : cv::kSha256Std) {
        const crypto_part part = {bufA, cv::hex(v.msg, bufA, sizeof(bufA))};
        crypto_sha256(&part, 1, digest);
        assertHex(v.digest, digest, 32, v.name);
    }
    for (const cv::HashVector &v : cv::kSha512Std) {
        const crypto_part part = {bufA, cv::hex(v.msg, bufA, sizeof(bufA))};
        crypto_sha512(&part, 1, digest);
        assertHex(v.digest, digest, 64, v.name);
    }
}

void test_sha_lengths_and_any_split_into_parts_give_the_same_digest(void)
{
    for (const cv::HashLenVector &v : cv::kHashLen) {
        char what[32];
        snprintf(what, sizeof(what), "len %d", v.len);
        uint8_t digest[64];
        cv::pattern(bufA, v.len);

        const crypto_part whole = {bufA, (size_t)v.len};
        crypto_sha256(&whole, 1, digest);
        assertHex(v.sha256, digest, 32, what);
        crypto_sha512(&whole, 1, digest);
        assertHex(v.sha512, digest, 64, what);

        const size_t half = v.len / 2;
        const crypto_part parts[] = {{bufA, half}, {nullptr, 0}, {bufA + half, v.len - half}};
        crypto_sha256(parts, 3, digest);
        assertHex(v.sha256, digest, 32, what);
        crypto_sha512(parts, 3, digest);
        assertHex(v.sha512, digest, 64, what);
    }
}

void test_sha_of_zero_parts_is_the_empty_digest(void)
{
    uint8_t digest[32];
    crypto_sha256(nullptr, 0, digest);
    assertHex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", digest, 32, "empty");
}

void test_hmac_sha256_rfc4231_whole_and_split(void)
{
    for (const cv::HmacVector &v : cv::kHmacSha256) {
        uint8_t key[131], digest[32];
        const size_t keyLen = cv::hex(v.key, key, sizeof(key));
        const size_t n = cv::hex(v.data, bufA, sizeof(bufA));

        const crypto_part whole = {bufA, n};
        crypto_hmac_sha256(key, keyLen, &whole, 1, digest);
        assertHex(v.mac, digest, cv::hexLen(v.mac), v.name);

        const crypto_part parts[] = {{bufA, n / 3}, {bufA + n / 3, n - n / 3}};
        crypto_hmac_sha256(key, keyLen, parts, 2, digest);
        assertHex(v.mac, digest, cv::hexLen(v.mac), v.name);
    }
}

// ---- Random -------------------------------------------------------------------------------------

void test_random_fills_the_buffer_and_stays_inside_it(void)
{
    uint8_t first[34], second[34];
    memset(first, 0xA5, sizeof(first));
    memset(second, 0xA5, sizeof(second));
    crypto_random_add(reinterpret_cast<const uint8_t *>("seed"), 4);
    TEST_ASSERT_TRUE(crypto_random(first + 1, 32));
    TEST_ASSERT_TRUE(crypto_random(second + 1, 32));
    TEST_ASSERT_EQUAL_HEX8(0xA5, first[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, first[33]);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(first + 1, second + 1, 32));
    TEST_ASSERT_TRUE(crypto_random(first, 0));
}

#if !(MESHTASTIC_EXCLUDE_PKI)
// ---- X25519 -------------------------------------------------------------------------------------

void test_x25519_rfc7748_section_5_2_with_unclamped_scalars(void)
{
    for (const cv::X25519Vector &v : cv::kX25519Std) {
        uint8_t scalar[32], point[32], out[32], scalarBefore[32];
        cv::hex(v.scalar, scalar, sizeof(scalar));
        cv::hex(v.point, point, sizeof(point));
        memcpy(scalarBefore, scalar, sizeof(scalar));
        TEST_ASSERT_TRUE_MESSAGE(crypto_x25519(out, scalar, point), v.name);
        assertHex(v.out, out, 32, v.name);
        assertMem(scalarBefore, scalar, 32, "clamping must not touch the caller's scalar");
    }
}

void test_x25519_rfc7748_one_iteration_from_base_point(void)
{
    uint8_t k[32], u[32], out[32];
    cv::hex(cv::kX25519Base, k, sizeof(k));
    cv::hex(cv::kX25519Base, u, sizeof(u));
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

    TEST_ASSERT_TRUE(crypto_x25519(out, a, nullptr));
    assertMem(aPub, out, 32);
    TEST_ASSERT_TRUE(crypto_x25519(out, b, nullptr));
    assertMem(bPub, out, 32);
    TEST_ASSERT_TRUE(crypto_x25519(out, a, bPub));
    assertMem(k, out, 32);
    TEST_ASSERT_TRUE(crypto_x25519(out, b, aPub));
    assertMem(k, out, 32);
}

void test_x25519_rejects_non_canonical_point(void)
{
    uint8_t scalar[32], point[32], out[32];
    cv::hex(cv::kRfc7748Dh[0], scalar, sizeof(scalar));
    memset(point, 0xFF, sizeof(point));
    point[0] = 0xED;
    point[31] = 0x7F;
    TEST_ASSERT_FALSE(crypto_x25519(out, scalar, point));
}

// ---- Golden wire vectors ------------------------------------------------------------------------

void test_golden_psk_channel_encryption(void)
{
    for (const cv::PskCtrVector &v : cv::kPskCtr) {
        uint8_t key[32], nonce[16];
        const size_t keyLen = cv::hex(v.key, key, sizeof(key));
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        buildNonce(nonce, v.from, v.packetId, 0);
        TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, nonce, bufA, n, bufA));
        assertHex(v.crypt, bufA, n, "psk ctr");
    }
}

void test_golden_psk_ccm_with_12_byte_tag_and_native_order_aad(void)
{
    for (const cv::PskCcmVector &v : cv::kPskCcm) {
        uint8_t key[32], nonce[16], aad[8], tag[12];
        const size_t keyLen = cv::hex(v.key, key, sizeof(key));
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        const size_t wireLen = cv::hex(v.cryptTag, bufC, sizeof(bufC));
        TEST_ASSERT_EQUAL(n + 12, wireLen);
        cv::hex(v.aad, aad, sizeof(aad));
        buildNonce(nonce, v.from, v.packetId, 0);

        TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, keyLen, nonce, aad, sizeof(aad), bufA, n, bufB, tag, sizeof(tag)));
        assertMem(bufC, bufB, n, "psk ccm ciphertext");
        assertMem(bufC + n, tag, sizeof(tag), "psk ccm tag");
        TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, keyLen, nonce, aad, sizeof(aad), bufC, n, bufC + n, sizeof(tag), bufB));
        assertMem(bufA, bufB, n, "psk ccm open");
    }
}

void test_golden_pki_key_derivation_8_byte_tag_and_trailing_extra_nonce(void)
{
    uint8_t priv[32], peer[32], secret[32], key[32];
    cv::hex(cv::kRfc7748Dh[0], priv, sizeof(priv));
    cv::hex(cv::kRfc7748Dh[3], peer, sizeof(peer));
    TEST_ASSERT_TRUE(crypto_x25519(secret, priv, peer));
    const crypto_part part = {secret, sizeof(secret)};
    crypto_sha256(&part, 1, key);
    assertHex(cv::kPkiSharedKey, key, 32, "shared key is SHA256 of the X25519 output");

    for (const cv::PkiVector &v : cv::kPki) {
        uint8_t nonce[16], tag[8];
        const size_t n = cv::hex(v.plain, bufA, sizeof(bufA));
        const size_t wireLen = cv::hex(v.wire, bufC, sizeof(bufC));
        TEST_ASSERT_EQUAL(n + 8 + 4, wireLen);
        buildNonce(nonce, v.from, v.packetId, v.extra);

        TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, sizeof(key), nonce, nullptr, 0, bufA, n, bufB, tag, sizeof(tag)));
        assertMem(bufC, bufB, n, "pki ciphertext");
        assertMem(bufC + n, tag, sizeof(tag), "pki tag");
        TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, sizeof(key), nonce, nullptr, 0, bufC, n, bufC + n, sizeof(tag), bufB));
        assertMem(bufA, bufB, n, "pki open");
    }
}

void test_golden_ack_proof_hmac_from_parts(void)
{
    uint8_t key[32], digest[32];
    cv::hex(cv::kPkiSharedKey, key, sizeof(key));
    for (const cv::AckProofVector &v : cv::kAckProof) {
        uint8_t header[3 + 12] = {'a', 'c', 'k'};
        const uint32_t fields[] = {v.ackFrom, v.ackTo, v.requestId};
        for (int f = 0; f < 3; f++)
            for (int i = 0; i < 4; i++)
                header[3 + 4 * f + i] = (uint8_t)(fields[f] >> (8 * i));
        const size_t n = cv::hex(v.routing, bufA, sizeof(bufA));
        const crypto_part parts[] = {{header, sizeof(header)}, {bufA, n}};
        crypto_hmac_sha256(key, sizeof(key), parts, 2, digest);
        assertHex(v.hmacSha256, digest, 32, "ack proof");
    }
}

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
// ---- Ed25519 and XEdDSA -------------------------------------------------------------------------

void test_ed25519_verify_rfc8032(void)
{
    for (const cv::Ed25519Vector &v : cv::kEd25519Std) {
        uint8_t pub[32], sig[64];
        cv::hex(v.pub, pub, sizeof(pub));
        cv::hex(v.sig, sig, sizeof(sig));
        const size_t n = cv::hex(v.msg, bufA + 1, sizeof(bufA) - 1);
        TEST_ASSERT_TRUE_MESSAGE(crypto_ed25519_verify(sig, pub, bufA + 1, n), v.name);
    }
}

void test_ed25519_verify_rejects_flipped_bits(void)
{
    const cv::Ed25519Vector &v = cv::kEd25519Std[2];
    uint8_t pub[32], sig[64];
    cv::hex(v.pub, pub, sizeof(pub));
    cv::hex(v.sig, sig, sizeof(sig));
    const size_t n = cv::hex(v.msg, bufA, sizeof(bufA));
    TEST_ASSERT_TRUE(crypto_ed25519_verify(sig, pub, bufA, n));
    for (size_t i = 0; i < sizeof(sig); i++) {
        sig[i] ^= (uint8_t)(1u << (i % 8));
        TEST_ASSERT_FALSE(crypto_ed25519_verify(sig, pub, bufA, n));
        sig[i] ^= (uint8_t)(1u << (i % 8));
    }
    for (size_t i = 0; i < n * 8; i++) {
        bufA[i / 8] ^= (uint8_t)(1u << (i % 8));
        TEST_ASSERT_FALSE(crypto_ed25519_verify(sig, pub, bufA, n));
        bufA[i / 8] ^= (uint8_t)(1u << (i % 8));
    }
}

void test_ed25519_verify_rejects_s_at_or_above_q(void)
{
    const cv::Ed25519Vector &v = cv::kEd25519Std[0];
    uint8_t pub[32], sig[64];
    cv::hex(v.pub, pub, sizeof(pub));
    cv::hex(cv::kEd25519MalleatedSig, sig, sizeof(sig));
    const size_t n = cv::hex(v.msg, bufA, sizeof(bufA));
    TEST_ASSERT_FALSE(crypto_ed25519_verify(sig, pub, bufA, n));
}

void test_ed25519_basemul_matches_rfc8032_test_1(void)
{
    // RFC 8032 7.1 TEST 1: its secret scalar a (mod q) times B is its public key.
    uint8_t scalar[32], point[32];
    cv::hex("7c2cac12e69be96ae9065065462385e8fcff2768d980c0a3a520f006904de90f", scalar, sizeof(scalar));
    TEST_ASSERT_TRUE(crypto_ed25519_basemul(point, scalar));
    assertHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", point, 32, "a * B");
}

static void loadAliceEdKeys(uint8_t edPriv[32], uint8_t edPub[32])
{
    uint8_t priv[32];
    cv::hex(cv::kRfc7748Dh[0], priv, sizeof(priv));
    priv[0] &= 0xF8;
    priv[31] = (priv[31] & 0x7F) | 0x40;
    crypto_xeddsa_keys_from_x25519(edPriv, edPub, priv);
}

void test_xeddsa_public_key_conversions_agree(void)
{
    uint8_t edPriv[32], edPub[32], curvePub[32], converted[32], viaBaseMul[32];
    loadAliceEdKeys(edPriv, edPub);
    cv::hex(cv::kRfc7748Dh[1], curvePub, sizeof(curvePub));
    crypto_xeddsa_pub_from_x25519(converted, curvePub);
    assertMem(edPub, converted, 32, "private and public conversion must land on the same key");
    TEST_ASSERT_TRUE(crypto_ed25519_basemul(viaBaseMul, edPriv));
    assertMem(edPub, viaBaseMul, 32, "the converted private key must produce the converted public key");
}

void test_xeddsa_sign_then_verify_randomized_and_tamper_evident(void)
{
    uint8_t edPriv[32], edPub[32], sig[64], sig2[64];
    loadAliceEdKeys(edPriv, edPub);
    for (size_t len : kLengths) {
        cv::pattern(bufA + 1, len);
        TEST_ASSERT_TRUE(crypto_xeddsa_sign(sig, edPriv, edPub, bufA + 1, len));
        TEST_ASSERT_TRUE(crypto_ed25519_verify(sig, edPub, bufA + 1, len));
        TEST_ASSERT_TRUE(crypto_xeddsa_sign(sig2, edPriv, edPub, bufA + 1, len));
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, memcmp(sig, sig2, 64), "signatures are hedged with a random Z");
        sig[0] ^= 1;
        TEST_ASSERT_FALSE(crypto_ed25519_verify(sig, edPub, bufA + 1, len));
    }
}

void test_golden_xeddsa_signature_verifies_over_the_signing_buffer_layout(void)
{
    uint8_t curvePub[32], edPub[32], sig[64];
    cv::hex(cv::kRfc7748Dh[1], curvePub, sizeof(curvePub));
    crypto_xeddsa_pub_from_x25519(edPub, curvePub);
    cv::hex(cv::kXeddsaGoldenSig, sig, sizeof(sig));

    // version | from | id | to | portnum | request_id | reply_id | emoji | bitfield | flags | payload
    const uint32_t fields[] = {0x929, 0x13B2D662, 0x1234, 1, 0xCAFE0001, 0xCAFE0002, 0, 1};
    size_t w = 0;
    bufA[w++] = 0x01;
    for (uint32_t f : fields)
        for (int i = 0; i < 4; i++)
            bufA[w++] = (uint8_t)(f >> (8 * i));
    bufA[w++] = 0x03; // want_response | has_bitfield
    cv::pattern(bufA + w, 17);
    w += 17;

    TEST_ASSERT_TRUE(crypto_ed25519_verify(sig, edPub, bufA, w));
    bufA[1] ^= 1;
    TEST_ASSERT_FALSE(crypto_ed25519_verify(sig, edPub, bufA, w));
}
#endif
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
    RUN_TEST(test_aes_ctr_sp800_38a_encrypt_and_decrypt);
    RUN_TEST(test_aes_ctr_lengths_match_independent_implementation);
    RUN_TEST(test_aes_ctr_in_place_and_unaligned_match_disjoint_output);
    RUN_TEST(test_aes_ctr_rejects_key_sizes_other_than_128_and_256);
    RUN_TEST(test_aes_ccm_standard_vectors);
    RUN_TEST(test_aes_ccm_lengths_match_independent_implementation);
    RUN_TEST(test_aes_ccm_in_place_and_unaligned_match_disjoint_output);
    RUN_TEST(test_aes_ccm_every_flipped_bit_is_rejected);
    RUN_TEST(test_aes_ccm_empty_payload_still_authenticates);
    RUN_TEST(test_aes_ccm_rejects_parameters_outside_the_format);
    RUN_TEST(test_sha_fips180_vectors);
    RUN_TEST(test_sha_lengths_and_any_split_into_parts_give_the_same_digest);
    RUN_TEST(test_sha_of_zero_parts_is_the_empty_digest);
    RUN_TEST(test_hmac_sha256_rfc4231_whole_and_split);
    RUN_TEST(test_random_fills_the_buffer_and_stays_inside_it);
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_x25519_rfc7748_section_5_2_with_unclamped_scalars);
    RUN_TEST(test_x25519_rfc7748_one_iteration_from_base_point);
    RUN_TEST(test_x25519_rfc7748_section_6_1_key_agreement);
    RUN_TEST(test_x25519_rejects_non_canonical_point);
    RUN_TEST(test_golden_psk_channel_encryption);
    RUN_TEST(test_golden_psk_ccm_with_12_byte_tag_and_native_order_aad);
    RUN_TEST(test_golden_pki_key_derivation_8_byte_tag_and_trailing_extra_nonce);
    RUN_TEST(test_golden_ack_proof_hmac_from_parts);
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    RUN_TEST(test_ed25519_verify_rfc8032);
    RUN_TEST(test_ed25519_verify_rejects_flipped_bits);
    RUN_TEST(test_ed25519_verify_rejects_s_at_or_above_q);
    RUN_TEST(test_ed25519_basemul_matches_rfc8032_test_1);
    RUN_TEST(test_xeddsa_public_key_conversions_agree);
    RUN_TEST(test_xeddsa_sign_then_verify_randomized_and_tamper_evident);
    RUN_TEST(test_golden_xeddsa_signature_verifies_over_the_signing_buffer_layout);
#endif
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
