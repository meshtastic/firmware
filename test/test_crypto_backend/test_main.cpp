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
// On ESP32 the hardware functions are also compared directly with the Crypto library, and the suite checks
// the backend chosen per primitive against a per-target table, so a dispatch that silently falls back to
// software fails.
//
// On nRF52840 the CC310 functions are also compared directly with the Crypto library, including every
// CCM key size, length, tag and AAD; flash-resident and unaligned buffers; and the edge keys whose verdict
// the software path used to give. crypto-nrf52840-backend links with --wrap, so the suite counts real calls
// per primitive and fails if a public function reaches the wrong backend, for example software AES-128.
//
// Regressions guarded: a changed nonce or AAD layout, tag length or CCM length field; in-place calls
// corrupting data (the old aes_ccm_* wrote the keystream into out before reading in, so the moved
// code must not); buffers misread when unaligned or at block boundaries; forged or tampered input
// being accepted, including a malleated Ed25519 S that the shared check must reject before any
// backend sees it; and crypto_x25519 not clamping, which RFC 7748 and the design both require.
#include "configuration.h"

#include "crypto/crypto_api.h"
#include "crypto/crypto_backends.h"
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
#ifdef ARCH_ESP32
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
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

// ---- Backend selection --------------------------------------------------------------------------

#if defined(ARCH_ESP32)
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define EXPECT_SHA512 "lib"
#else
#define EXPECT_SHA512 "esp-sha"
#endif
#define EXPECT_AES "esp-aes"
#define EXPECT_SHA256 "esp-sha"
#define EXPECT_CURVES "lib"
#elif defined(ARCH_NRF52) && !defined(ARCH_NRF54L)
#define EXPECT_AES "lib"
#define EXPECT_SHA256 "lib"
#define EXPECT_SHA512 "lib"
#define EXPECT_CURVES "cc310"
#else
#define EXPECT_AES "lib"
#define EXPECT_SHA256 "lib"
#define EXPECT_SHA512 "lib"
#define EXPECT_CURVES "lib"
#endif

#if defined(ARCH_ESP32)
#define ACTUAL_AES "esp-aes"
#define ACTUAL_SHA256 "esp-sha"
#else
#define ACTUAL_AES "lib"
#define ACTUAL_SHA256 "lib"
#endif
#ifdef CRYPTO_ESP32_HAS_SHA512
#define ACTUAL_SHA512 "esp-sha"
#else
#define ACTUAL_SHA512 "lib"
#endif
#ifdef CRYPTO_HAS_CC310
#define ACTUAL_CURVES "cc310"
#else
#define ACTUAL_CURVES "lib"
#endif

void test_backend_per_primitive_is_the_one_expected_for_this_target(void)
{
    struct BackendName {
        const char *primitive, *actual, *expected;
    };
    const BackendName names[] = {
        {"aes-ctr", ACTUAL_AES, EXPECT_AES},       {"aes-ccm", ACTUAL_AES, EXPECT_AES},
        {"sha-256", ACTUAL_SHA256, EXPECT_SHA256}, {"hmac-sha-256", ACTUAL_SHA256, EXPECT_SHA256},
        {"sha-512", ACTUAL_SHA512, EXPECT_SHA512}, {"curves", ACTUAL_CURVES, EXPECT_CURVES},
    };
    for (const BackendName &n : names) {
        char line[64];
        snprintf(line, sizeof(line), "backend %s = %s", n.primitive, n.actual);
        TEST_MESSAGE(line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(n.expected, n.actual, n.primitive);
    }
}

#ifdef ARCH_ESP32
// ---- ESP32: hardware against the Crypto library ---------------------------------------------------

static const size_t kHwLengths[] = {0,   1,   15,  16,  17,  63,  64,  65,  127, 128, 129, 240, 241, 242, 243,
                                    244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 1024};
static const size_t kTagLengths[] = {4, 8, 12, 16};
static const size_t kAadLengths[] = {0, 1, 13, 30};
static constexpr size_t HW_BUF = 1024 + 16;

static uint8_t hwAad[32], hwIn[HW_BUF], hwHw[HW_BUF], hwLib[HW_BUF], hwTmp[HW_BUF], hwTagA[16], hwTagB[16];

static void describe(char *what, size_t cap, size_t keyLen, size_t len, size_t tagLen = 0, size_t aadLen = 0)
{
    snprintf(what, cap, "AES-%d len %d M=%d aad=%d", (int)keyLen * 8, (int)len, (int)tagLen, (int)aadLen);
}

void test_esp32_aes_ctr_matches_library_for_every_key_size_and_length(void)
{
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : kHwLengths) {
            char what[48];
            describe(what, sizeof(what), keyLen, len);
            cv::pattern(hwIn, len);
            TEST_ASSERT_TRUE_MESSAGE(crypto_esp32_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwIn, len, hwHw), what);
            TEST_ASSERT_TRUE_MESSAGE(crypto_lib_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwIn, len, hwLib), what);
            assertMem(hwLib, hwHw, len, what);
            memcpy(hwTmp, hwIn, len);
            TEST_ASSERT_TRUE_MESSAGE(crypto_esp32_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwTmp, len, hwTmp), what);
            assertMem(hwLib, hwTmp, len, what);
        }
    }
}

void test_esp32_aes_ccm_matches_library_for_every_key_size_length_tag_and_aad(void)
{
    cv::pattern(hwAad, sizeof(hwAad));
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : kHwLengths) {
            cv::pattern(hwIn, len);
            for (size_t tagLen : kTagLengths) {
                for (size_t aadLen : kAadLengths) {
                    char what[48];
                    describe(what, sizeof(what), keyLen, len, tagLen, aadLen);
                    const uint8_t *aad = aadLen ? hwAad : nullptr;

                    TEST_ASSERT_TRUE_MESSAGE(
                        crypto_esp32_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwIn, len, hwHw, hwTagA, tagLen),
                        what);
                    crypto_lib_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwIn, len, hwLib, hwTagB, tagLen);
                    assertMem(hwLib, hwHw, len, what);
                    assertMem(hwTagB, hwTagA, tagLen, what);

                    TEST_ASSERT_TRUE_MESSAGE(
                        crypto_esp32_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwLib, len, hwTagB, tagLen, hwTmp),
                        what);
                    assertMem(hwIn, hwTmp, len, what);
                    TEST_ASSERT_TRUE_MESSAGE(
                        crypto_lib_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwHw, len, hwTagA, tagLen, hwTmp),
                        what);
                    assertMem(hwIn, hwTmp, len, what);

                    hwTagA[0] ^= 1;
                    TEST_ASSERT_FALSE_MESSAGE(
                        crypto_esp32_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwHw, len, hwTagA, tagLen, hwTmp),
                        what);
                }
            }
        }
    }
}

void test_esp32_sha_and_hmac_match_library_for_every_length_and_split(void)
{
    uint8_t keyBytes[100];
    cv::pattern(keyBytes, sizeof(keyBytes));
    for (size_t len : kHwLengths) {
        cv::pattern(hwIn, len);
        const size_t cut = len / 3;
        const crypto_part parts[3] = {{hwIn, cut}, {hwIn + cut, len / 3}, {hwIn + cut + len / 3, len - cut - len / 3}};
        uint8_t a[64], b[64];

        crypto_esp32_sha256(parts, 3, a);
        crypto_lib_sha256(parts, 3, b);
        assertMem(b, a, 32, "sha-256");
        crypto_esp32_sha512(parts, 3, a);
        crypto_lib_sha512(parts, 3, b);
        assertMem(b, a, 64, "sha-512");
        for (size_t keyLen : {(size_t)0, (size_t)1, (size_t)32, (size_t)64, (size_t)65, (size_t)100}) {
            crypto_esp32_hmac_sha256(keyBytes, keyLen, parts, 3, a);
            crypto_lib_hmac_sha256(keyBytes, keyLen, parts, 3, b);
            assertMem(b, a, 32, "hmac-sha-256");
        }
    }
}

// constexpr data lands in .rodata, which is flash-mapped and which a DMA engine cannot read directly.
struct FlashBytes {
    uint8_t key[32], data[256];
    constexpr FlashBytes() : key(), data()
    {
        for (size_t i = 0; i < 32; i++)
            key[i] = (uint8_t)i;
        for (size_t i = 0; i < 256; i++)
            data[i] = (uint8_t)(i * 7 + 3);
    }
};
static constexpr FlashBytes kFlash;

static void runAllPrimitives(const uint8_t *key, size_t keyLen, const uint8_t *in, uint8_t *out, uint8_t *digest)
{
    const crypto_part part = {in, 256};
    TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, in, 256, out));
    TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, in, 13, in, 256, out + 256, out + 512, 8));
    crypto_sha256(&part, 1, digest);
    crypto_sha512(&part, 1, digest + 32);
    crypto_hmac_sha256(key, keyLen, &part, 1, digest + 96);
}

void test_esp32_key_and_input_in_flash_give_the_same_results_as_in_ram(void)
{
    static uint8_t ramKey[32], ramData[256], outFlash[528], outRam[528], digFlash[128], digRam[128];
    memcpy(ramKey, kFlash.key, sizeof(ramKey));
    memcpy(ramData, kFlash.data, sizeof(ramData));
    cv::pattern(hwIn, 256);
    assertMem(hwIn, kFlash.data, 256, "flash data is the pattern");
    for (size_t keyLen : kKeyLengths) {
        runAllPrimitives(kFlash.key, keyLen, kFlash.data, outFlash, digFlash);
        runAllPrimitives(ramKey, keyLen, ramData, outRam, digRam);
        assertMem(outRam, outFlash, 520, "ctr and ccm");
        assertMem(digRam, digFlash, 128, "hashes");
    }
}

void test_esp32_unaligned_in_place_and_hash_inputs_match_library_at_block_boundaries(void)
{
    static const size_t lens[] = {31, 32, 33, 63, 64, 65, 127, 128, 129, 191, 192, 193};
    uint8_t key[32];
    cv::seqKey(key, 32);
    for (size_t len : lens) {
        cv::pattern(hwIn, len);
        crypto_lib_aes_ctr(key, 32, cv::kLenCtrBlock, hwIn, len, hwLib);
        crypto_lib_aes_ccm_seal(key, 32, cv::kLenCcmNonce, nullptr, 0, hwIn, len, hwLib + 256, hwTagB, 8);
        const crypto_part ref = {hwIn, len};
        uint8_t digestLib[32], digestHw[32];
        crypto_lib_sha256(&ref, 1, digestLib);
        for (size_t offset = 0; offset < 4; offset++) {
            memcpy(hwTmp + offset, hwIn, len);
            TEST_ASSERT_TRUE(crypto_esp32_aes_ctr(key, 32, cv::kLenCtrBlock, hwTmp + offset, len, hwTmp + offset));
            assertMem(hwLib, hwTmp + offset, len, "ctr in place");

            memcpy(hwTmp + offset, hwIn, len);
            TEST_ASSERT_TRUE(
                crypto_esp32_aes_ccm_seal(key, 32, cv::kLenCcmNonce, nullptr, 0, hwTmp + offset, len, hwTmp + offset, hwTagA, 8));
            assertMem(hwLib + 256, hwTmp + offset, len, "ccm in place");
            assertMem(hwTagB, hwTagA, 8, "ccm tag");
            TEST_ASSERT_TRUE(
                crypto_esp32_aes_ccm_open(key, 32, cv::kLenCcmNonce, nullptr, 0, hwTmp + offset, len, hwTagA, 8, hwTmp + offset));
            assertMem(hwIn, hwTmp + offset, len, "ccm open in place");

            const crypto_part part = {hwTmp + offset, len};
            crypto_esp32_sha256(&part, 1, digestHw);
            assertMem(digestLib, digestHw, 32, "sha-256 unaligned");
        }
    }
}

void test_esp32_psram_buffers_give_the_same_results_as_internal_ram(void)
{
    if (!psramFound())
        TEST_IGNORE_MESSAGE("no PSRAM on this board");
    uint8_t *ps = (uint8_t *)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
    TEST_ASSERT_NOT_NULL(ps);
    uint8_t *psKey = ps, *psIn = ps + 64, *psOut = ps + 512, *psDigest = ps + 1536;
    static uint8_t ramOut[528], ramDigest[128];
    uint8_t key[32];
    cv::seqKey(key, 32);
    memcpy(psKey, key, 32);
    cv::pattern(psIn, 256);
    runAllPrimitives(psKey, 32, psIn, psOut, psDigest);
    cv::pattern(hwIn, 256);
    runAllPrimitives(key, 32, hwIn, ramOut, ramDigest);
    assertMem(ramOut, psOut, 520, "ctr and ccm");
    assertMem(ramDigest, psDigest, 128, "hashes");
    heap_caps_free(ps);
}

// ---- ESP32: shared engine across tasks ------------------------------------------------------------

struct LockTask {
    volatile int failures;
    SemaphoreHandle_t done;
};

static uint8_t lockKey[32], lockCounter[16], lockPlain[64], lockCrypt[64], lockCcmKey[32], lockCcmNonce[13], lockCcmAad[16],
    lockCcmPlain[24], lockCcmCrypt[24], lockCcmTag[16], lockAbcDigest[32];
static size_t lockKeyLen, lockCcmKeyLen, lockCcmAadLen, lockCcmLen, lockCcmTagLen;
static constexpr int LOCK_ROUNDS = 400;

static void lockWorker(void *arg)
{
    LockTask *t = (LockTask *)arg;
    const uint8_t abc[3] = {'a', 'b', 'c'};
    for (int i = 0; i < LOCK_ROUNDS; i++) {
        uint8_t out[64], tag[16], digest[32];
        if (!crypto_aes_ctr(lockKey, lockKeyLen, lockCounter, lockPlain, sizeof(lockPlain), out) ||
            memcmp(out, lockCrypt, sizeof(lockCrypt)) != 0)
            t->failures++;
        const crypto_part part = {abc, 3};
        crypto_sha256(&part, 1, digest);
        if (memcmp(digest, lockAbcDigest, 32) != 0)
            t->failures++;
        if (!crypto_aes_ccm_seal(lockCcmKey, lockCcmKeyLen, lockCcmNonce, lockCcmAad, lockCcmAadLen, lockCcmPlain, lockCcmLen,
                                 out, tag, lockCcmTagLen) ||
            memcmp(out, lockCcmCrypt, lockCcmLen) != 0 || memcmp(tag, lockCcmTag, lockCcmTagLen) != 0)
            t->failures++;
    }
    xSemaphoreGive(t->done);
    vTaskDelete(nullptr);
}

void test_esp32_concurrent_tasks_get_correct_aes_and_sha_results(void)
{
    const cv::CtrVector &c = cv::kCtrStd[1];
    lockKeyLen = cv::hex(c.key, lockKey, sizeof(lockKey));
    cv::hex(c.counter, lockCounter, sizeof(lockCounter));
    TEST_ASSERT_EQUAL(64, cv::hex(c.plain, lockPlain, sizeof(lockPlain)));
    cv::hex(c.crypt, lockCrypt, sizeof(lockCrypt));
    const cv::CcmVector &m = cv::kCcmStd[0];
    lockCcmKeyLen = cv::hex(m.key, lockCcmKey, sizeof(lockCcmKey));
    cv::hex(m.nonce, lockCcmNonce, sizeof(lockCcmNonce));
    lockCcmAadLen = cv::hex(m.aad, lockCcmAad, sizeof(lockCcmAad));
    lockCcmLen = cv::hex(m.plain, lockCcmPlain, sizeof(lockCcmPlain));
    cv::hex(m.crypt, lockCcmCrypt, sizeof(lockCcmCrypt));
    lockCcmTagLen = cv::hex(m.tag, lockCcmTag, sizeof(lockCcmTag));
    cv::hex(cv::kSha256Std[1].digest, lockAbcDigest, sizeof(lockAbcDigest));

    LockTask tasks[2] = {{0, xSemaphoreCreateBinary()}, {0, xSemaphoreCreateBinary()}};
    for (int i = 0; i < 2; i++) {
        const BaseType_t core = SOC_CPU_CORES_NUM > 1 ? i : tskNO_AFFINITY;
        TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(lockWorker, "crypto_lock", 8192, &tasks[i], 1, nullptr, core));
    }
    for (LockTask &t : tasks)
        TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(t.done, pdMS_TO_TICKS(60000)));
    TEST_ASSERT_EQUAL(0, tasks[0].failures);
    TEST_ASSERT_EQUAL(0, tasks[1].failures);
    vSemaphoreDelete(tasks[0].done);
    vSemaphoreDelete(tasks[1].done);
}
#endif
// ---- Long lengths, on every target -------------------------------------------------------------

static const size_t kLongLengths[] = {240, 241, 242, 243, 244, 245, 246, 247, 248, 249,
                                      250, 251, 252, 253, 254, 255, 256, 257, 512, 1024};
static constexpr size_t LONG_BUF = 1024 + 16;
static uint8_t longIn[LONG_BUF], longOut[LONG_BUF], longRef[LONG_BUF];

// The old CC310 path passed the length through a uint8_t, so 241-256 wrapped to a short run.
void test_aes_ctr_long_lengths_equal_block_by_block_counter(void)
{
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : kLongLengths) {
            cv::pattern(longIn, len);
            memset(longOut + len, 0xA5, 16);
            TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, longIn, len, longOut));
            uint8_t counter[16];
            memcpy(counter, cv::kLenCtrBlock, sizeof(counter));
            for (size_t off = 0; off < len; off += 16) {
                TEST_ASSERT_TRUE(
                    crypto_aes_ctr(key, keyLen, counter, longIn + off, len - off < 16 ? len - off : 16, longRef + off));
                for (int i = 15; i >= 12 && ++counter[i] == 0; i--) {
                }
            }
            assertMem(longRef, longOut, len, "long ctr");
            TEST_ASSERT_EACH_EQUAL_UINT8(0xA5, longOut + len, 16);
        }
    }
}

void test_aes_ccm_long_lengths_round_trip_and_reject_tamper(void)
{
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32], tag[16];
        cv::seqKey(key, keyLen);
        for (size_t len : kLongLengths) {
            cv::pattern(longIn, len);
            TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, nullptr, 0, longIn, len, longOut, tag, 8));
            TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, nullptr, 0, longOut, len, tag, 8, longRef));
            assertMem(longIn, longRef, len, "long ccm");
            longOut[len - 1] ^= 1;
            TEST_ASSERT_FALSE(crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, nullptr, 0, longOut, len, tag, 8, longRef));
        }
    }
}

#if !(MESHTASTIC_EXCLUDE_PKI)
// generateKeyPair and regeneratePublicKey rely on this: a stale out would pass as a public key.
void test_x25519_failure_leaves_out_all_zero(void)
{
    uint8_t scalar[32], point[32], out[32];
    cv::hex(cv::kRfc7748Dh[0], scalar, sizeof(scalar));
    memset(point, 0xFF, sizeof(point));
    point[0] = 0xED;
    point[31] = 0x7F;
    memset(out, 0xA5, sizeof(out));
    TEST_ASSERT_FALSE(crypto_x25519(out, scalar, point));
    TEST_ASSERT_EACH_EQUAL_UINT8(0, out, sizeof(out));
}
#endif

#ifdef CRYPTO_HAS_CC310
// ---- nRF52840: CC310 against the Crypto library ---------------------------------------------------

static const size_t kHwLengths[] = {0,   1,   15,  16,  17,  63,  64,  65,  127, 128, 129, 240, 241, 242, 243,
                                    244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 1024};
static const size_t kTagLengths[] = {4, 8, 12, 16};
static const size_t kAadLengths[] = {0, 1, 13, 14, 15, 30};
static constexpr size_t HW_BUF = 1024 + 16;

static uint8_t hwAad[32], hwIn[HW_BUF], hwHw[HW_BUF], hwLib[HW_BUF], hwTmp[HW_BUF], hwTagA[16], hwTagB[16];

static void describe(char *what, size_t cap, size_t keyLen, size_t len, size_t tagLen = 0, size_t aadLen = 0)
{
    snprintf(what, cap, "AES-%d len %d M=%d aad=%d", (int)keyLen * 8, (int)len, (int)tagLen, (int)aadLen);
}

void test_nrf52_aes_ctr_matches_library_for_every_key_size_and_length(void)
{
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : kHwLengths) {
            char what[48];
            describe(what, sizeof(what), keyLen, len);
            cv::pattern(hwIn, len);
            TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwIn, len, hwHw), what);
            TEST_ASSERT_TRUE_MESSAGE(crypto_lib_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwIn, len, hwLib), what);
            assertMem(hwLib, hwHw, len, what);
            memcpy(hwTmp, hwIn, len);
            TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwTmp, len, hwTmp), what);
            assertMem(hwLib, hwTmp, len, what);
        }
    }
}

void test_nrf52_aes_ccm_matches_library_for_every_key_size_length_tag_and_aad(void)
{
    cv::pattern(hwAad, sizeof(hwAad));
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : kHwLengths) {
            cv::pattern(hwIn, len);
            for (size_t tagLen : kTagLengths) {
                for (size_t aadLen : kAadLengths) {
                    char what[48];
                    describe(what, sizeof(what), keyLen, len, tagLen, aadLen);
                    const uint8_t *aad = aadLen ? hwAad : nullptr;

                    TEST_ASSERT_TRUE_MESSAGE(
                        crypto_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwIn, len, hwHw, hwTagA, tagLen), what);
                    crypto_lib_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwIn, len, hwLib, hwTagB, tagLen);
                    assertMem(hwLib, hwHw, len, what);
                    assertMem(hwTagB, hwTagA, tagLen, what);

                    TEST_ASSERT_TRUE_MESSAGE(
                        crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwLib, len, hwTagB, tagLen, hwTmp), what);
                    assertMem(hwIn, hwTmp, len, what);
                    TEST_ASSERT_TRUE_MESSAGE(
                        crypto_lib_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwHw, len, hwTagA, tagLen, hwTmp),
                        what);
                    assertMem(hwIn, hwTmp, len, what);

                    hwTagA[0] ^= 1;
                    TEST_ASSERT_FALSE_MESSAGE(
                        crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwHw, len, hwTagA, tagLen, hwTmp), what);
                    if (len) {
                        hwTagA[0] ^= 1;
                        hwHw[len / 2] ^= 0x40;
                        TEST_ASSERT_FALSE_MESSAGE(
                            crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwHw, len, hwTagA, tagLen, hwTmp),
                            what);
                        if (keyLen == 16)
                            TEST_ASSERT_EACH_EQUAL_UINT8_MESSAGE(0, hwTmp, len, "failed open wipes out");
                    }
                    if (aadLen) {
                        hwAad[0] ^= 1;
                        crypto_lib_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwIn, len, hwHw, hwTagA, tagLen);
                        hwAad[0] ^= 1;
                        TEST_ASSERT_FALSE_MESSAGE(
                            crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, aad, aadLen, hwHw, len, hwTagA, tagLen, hwTmp),
                            what);
                    }
                }
            }
        }
    }
}

void test_nrf52_sha_and_hmac_match_library_for_every_length_and_split(void)
{
    uint8_t keyBytes[100];
    cv::pattern(keyBytes, sizeof(keyBytes));
    for (size_t len : kHwLengths) {
        cv::pattern(hwIn, len);
        const size_t cut = len / 3;
        const crypto_part parts[3] = {{hwIn, cut}, {hwIn + cut, len / 3}, {hwIn + cut + len / 3, len - cut - len / 3}};
        uint8_t a[64], b[64];

        crypto_sha256(parts, 3, a);
        crypto_lib_sha256(parts, 3, b);
        assertMem(b, a, 32, "sha-256");
        crypto_sha512(parts, 3, a);
        crypto_lib_sha512(parts, 3, b);
        assertMem(b, a, 64, "sha-512");
        for (size_t keyLen : {(size_t)0, (size_t)1, (size_t)32, (size_t)64, (size_t)65, (size_t)100}) {
            crypto_hmac_sha256(keyBytes, keyLen, parts, 3, a);
            crypto_lib_hmac_sha256(keyBytes, keyLen, parts, 3, b);
            assertMem(b, a, 32, "hmac-sha-256");
        }
    }
}

// constexpr data lands in .rodata, in flash, which the CC310 DMA cannot read.
struct FlashBytes {
    uint8_t key[32], data[256];
    constexpr FlashBytes() : key(), data()
    {
        for (size_t i = 0; i < 32; i++)
            key[i] = (uint8_t)i;
        for (size_t i = 0; i < 256; i++)
            data[i] = (uint8_t)(i * 7 + 3);
    }
};
static constexpr FlashBytes kFlash;

static void runAllPrimitives(const uint8_t *key, size_t keyLen, const uint8_t *in, uint8_t *out, uint8_t *digest)
{
    const crypto_part part = {in, 256};
    TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, in, 256, out));
    TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, in, 13, in, 256, out + 256, out + 512, 8));
    crypto_sha256(&part, 1, digest);
    crypto_sha512(&part, 1, digest + 32);
    crypto_hmac_sha256(key, keyLen, &part, 1, digest + 96);
}

void test_nrf52_key_and_input_in_flash_give_the_same_results_as_in_ram(void)
{
    TEST_ASSERT_TRUE((uintptr_t)kFlash.data < 0x20000000);
    static uint8_t ramKey[32], ramData[256], outFlash[528], outRam[528], digFlash[128], digRam[128];
    memcpy(ramKey, kFlash.key, sizeof(ramKey));
    memcpy(ramData, kFlash.data, sizeof(ramData));
    for (size_t keyLen : kKeyLengths) {
        runAllPrimitives(kFlash.key, keyLen, kFlash.data, outFlash, digFlash);
        runAllPrimitives(ramKey, keyLen, ramData, outRam, digRam);
        assertMem(outRam, outFlash, 520, "ctr and ccm");
        assertMem(digRam, digFlash, 128, "hashes");
    }
}

void test_nrf52_unaligned_and_in_place_match_library_at_block_boundaries(void)
{
    static const size_t lens[] = {31, 32, 33, 63, 64, 65, 127, 128, 129, 191, 192, 193, 256, 257};
    for (size_t keyLen : kKeyLengths) {
        uint8_t key[32];
        cv::seqKey(key, keyLen);
        for (size_t len : lens) {
            cv::pattern(hwIn, len);
            crypto_lib_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwIn, len, hwLib);
            crypto_lib_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, nullptr, 0, hwIn, len, hwLib + 300, hwTagB, 8);
            const crypto_part ref = {hwIn, len};
            uint8_t digestLib[32], digestHw[32];
            crypto_lib_sha256(&ref, 1, digestLib);
            for (size_t offset = 0; offset < 4; offset++) {
                memcpy(hwTmp + offset, hwIn, len);
                TEST_ASSERT_TRUE(crypto_aes_ctr(key, keyLen, cv::kLenCtrBlock, hwTmp + offset, len, hwTmp + offset));
                assertMem(hwLib, hwTmp + offset, len, "ctr in place");

                memcpy(hwTmp + offset, hwIn, len);
                TEST_ASSERT_TRUE(crypto_aes_ccm_seal(key, keyLen, cv::kLenCcmNonce, nullptr, 0, hwTmp + offset, len,
                                                     hwTmp + offset, hwTagA, 8));
                assertMem(hwLib + 300, hwTmp + offset, len, "ccm in place");
                assertMem(hwTagB, hwTagA, 8, "ccm tag");
                TEST_ASSERT_TRUE(crypto_aes_ccm_open(key, keyLen, cv::kLenCcmNonce, nullptr, 0, hwTmp + offset, len, hwTagA, 8,
                                                     hwTmp + offset));
                assertMem(hwIn, hwTmp + offset, len, "ccm open in place");

                const crypto_part part = {hwTmp + offset, len};
                crypto_sha256(&part, 1, digestHw);
                assertMem(digestLib, digestHw, 32, "sha-256 unaligned");
            }
        }
    }
}

#if !(MESHTASTIC_EXCLUDE_PKI)
static bool x25519Rejected(bool ok, const uint8_t out[32])
{
    return !ok || crypto_x25519_is_weak(out);
}

void test_nrf52_x25519_matches_library_for_canonical_points_and_top_bit(void)
{
    uint8_t scalar[32], point[32], hw[32], lib[32];
    cv::hex(cv::kRfc7748Dh[0], scalar, sizeof(scalar));
    scalar[0] &= 0xF8;
    scalar[31] = (scalar[31] & 0x7F) | 0x40;
    for (int i = 0; i < 16; i++) {
        memset(point, 0x11 * (i + 1), sizeof(point));
        point[0] = (uint8_t)(i * 13 + 9);
        TEST_ASSERT_TRUE(crypto_nrf52_x25519(hw, scalar, point));
        TEST_ASSERT_TRUE(crypto_lib_x25519(lib, scalar, point));
        assertMem(lib, hw, 32, "x25519");
    }
    TEST_ASSERT_TRUE(crypto_nrf52_x25519(hw, scalar, nullptr));
    TEST_ASSERT_TRUE(crypto_lib_x25519(lib, scalar, nullptr));
    assertMem(lib, hw, 32, "x25519 base point");
}

// isCanonicalPoint and the all-zero result decide which peer keys a node accepts. The software path
// used to answer whenever the CC310 returned an error, so what is rejected must still be rejected.
void test_nrf52_x25519_rejects_exactly_what_the_library_rejects_on_edge_points(void)
{
    static const char *const edge[] = {
        "0000000000000000000000000000000000000000000000000000000000000000", // u = 0
        "0100000000000000000000000000000000000000000000000000000000000000", // u = 1
        "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", // u = -1
        "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800", // order 8
        "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157", // order 8
        "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", // u = p
        "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", // u = p + 1
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", // u = 2^255 - 1
    };
    uint8_t scalar[32], point[32], hw[32], lib[32];
    cv::hex(cv::kRfc7748Dh[0], scalar, sizeof(scalar));
    scalar[0] &= 0xF8;
    scalar[31] = (scalar[31] & 0x7F) | 0x40;
    for (const char *h : edge) {
        cv::hex(h, point, sizeof(point));
        const bool hwOk = crypto_nrf52_x25519(hw, scalar, point);
        const bool libOk = crypto_lib_x25519(lib, scalar, point);
        TEST_ASSERT_TRUE_MESSAGE(x25519Rejected(libOk, lib), h);
        TEST_ASSERT_TRUE_MESSAGE(x25519Rejected(hwOk, hw), h);
    }
}

void test_nrf52_x25519_scalar_and_point_in_flash(void)
{
    uint8_t scalar[32], point[32], fromFlash[32], fromRam[32];
    memcpy(scalar, kFlash.key, sizeof(scalar));
    memcpy(point, kFlash.data, sizeof(point));
    scalar[0] &= 0xF8;
    scalar[31] = (scalar[31] & 0x7F) | 0x40;
    TEST_ASSERT_TRUE(crypto_x25519(fromFlash, kFlash.key, kFlash.data));
    TEST_ASSERT_TRUE(crypto_nrf52_x25519(fromRam, scalar, point));
    assertMem(fromRam, fromFlash, 32, "x25519 flash");
}

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
// RFC 8032 7.1 TEST 3, as bytes so that it stays in flash.
static const uint8_t kFlashEdPub[32] = {0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a,
                                        0xa7, 0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4,
                                        0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c};
static const uint8_t kFlashEdSig[64] = {0x62, 0x91, 0xd6, 0x57, 0xde, 0xec, 0x24, 0x02, 0x48, 0x27, 0xe6, 0x9c, 0x3a,
                                        0xbe, 0x01, 0xa3, 0x0c, 0xe5, 0x48, 0xa2, 0x84, 0x74, 0x3a, 0x44, 0x5e, 0x36,
                                        0x80, 0xd7, 0xdb, 0x5a, 0xc3, 0xac, 0x18, 0xff, 0x9b, 0x53, 0x8d, 0x16, 0xf2,
                                        0x90, 0xae, 0x67, 0xf7, 0x60, 0x98, 0x4d, 0xc6, 0x59, 0x4a, 0x7c, 0x15, 0xe9,
                                        0x71, 0x6e, 0xd2, 0x8d, 0xc0, 0x27, 0xbe, 0xce, 0xea, 0x1e, 0xc4, 0x0a};
static const uint8_t kFlashEdMsg[2] = {0xaf, 0x82};

void test_nrf52_ed25519_verify_rfc8032_through_flash_and_unaligned_buffers(void)
{
    TEST_ASSERT_TRUE(crypto_ed25519_verify(kFlashEdSig, kFlashEdPub, kFlashEdMsg, sizeof(kFlashEdMsg)));
    for (const cv::Ed25519Vector &v : cv::kEd25519Std) {
        uint8_t pub[32], sig[64];
        cv::hex(v.pub, pub, sizeof(pub));
        cv::hex(v.sig, sig, sizeof(sig));
        const size_t n = cv::hex(v.msg, hwIn, sizeof(hwIn));
        TEST_ASSERT_TRUE_MESSAGE(crypto_ed25519_verify(sig, pub, hwIn, n), v.name);
        for (size_t offset = 1; offset < 4; offset++) {
            memmove(hwTmp + offset, hwIn, n);
            TEST_ASSERT_TRUE_MESSAGE(crypto_ed25519_verify(sig, pub, hwTmp + offset, n), v.name);
            if (n) {
                hwTmp[offset] ^= 1;
                TEST_ASSERT_FALSE_MESSAGE(crypto_ed25519_verify(sig, pub, hwTmp + offset, n), v.name);
            }
        }
    }
}

// The software path used to decide every CC310 error other than "verify failed". A node must keep
// giving the library's verdict on these keys, or dropping that path changed what it accepts.
void test_nrf52_ed25519_verify_gives_the_library_verdict_on_edge_keys(void)
{
    static const char *const pubs[] = {
        "0100000000000000000000000000000000000000000000000000000000000000", // y = 1, identity
        "0000000000000000000000000000000000000000000000000000000000000000", // y = 0, order 4
        "0000000000000000000000000000000000000000000000000000000000000080", // y = 0, x sign set
        "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", // y = -1, order 2
        "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", // y = p, non-canonical
        "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", // y = p + 1, non-canonical
        "0200000000000000000000000000000000000000000000000000000000000000", // y = 2, not on the curve
    };
    static const uint8_t msg[4] = {1, 2, 3, 4};
    uint8_t pub[32], sig[64] = {0};
    for (const char *h : pubs) {
        cv::hex(h, pub, sizeof(pub));
        // R = identity, S = 0 is the classic forgery for a small-order key.
        memset(sig, 0, sizeof(sig));
        sig[0] = 1;
        TEST_ASSERT_EQUAL_MESSAGE(crypto_lib_ed25519_verify(sig, pub, msg, sizeof(msg)),
                                  crypto_nrf52_ed25519_verify(sig, pub, msg, sizeof(msg)), h);
    }
}

void test_nrf52_ed25519_basemul_matches_library_and_refuses_unreduced_scalars(void)
{
    uint8_t scalar[32], hw[32], lib[32];
    for (int i = 0; i < 8; i++) {
        cv::pattern(scalar, sizeof(scalar));
        scalar[0] ^= (uint8_t)i;
        scalar[31] = (uint8_t)(i * 2);
        TEST_ASSERT_TRUE(crypto_nrf52_ed25519_basemul(hw, scalar));
        crypto_lib_ed25519_basemul(lib, scalar);
        assertMem(lib, hw, 32, "r * B");
    }
    memset(scalar, 0, sizeof(scalar));
    scalar[0] = 1;
    TEST_ASSERT_TRUE(crypto_nrf52_ed25519_basemul(hw, scalar));
    crypto_lib_ed25519_basemul(lib, scalar);
    assertMem(lib, hw, 32, "1 * B");
    memset(scalar, 0xFF, sizeof(scalar));
    TEST_ASSERT_FALSE(crypto_nrf52_ed25519_basemul(hw, scalar));
}

// XEdDSA takes R from the CC310 and everything else from portable code, so the software verifier checks it.
void test_nrf52_xeddsa_signature_with_cc310_R_verifies_in_software(void)
{
    uint8_t priv[32], edPriv[32], edPub[32], sig[64];
    cv::hex(cv::kRfc7748Dh[0], priv, sizeof(priv));
    crypto_xeddsa_keys_from_x25519(edPriv, edPub, priv);
    static const uint8_t msg[] = "xeddsa over cc310";
    TEST_ASSERT_TRUE(crypto_xeddsa_sign(sig, edPriv, edPub, msg, sizeof(msg)));
    TEST_ASSERT_TRUE(crypto_lib_ed25519_verify(sig, edPub, msg, sizeof(msg)));
    TEST_ASSERT_TRUE(crypto_nrf52_ed25519_verify(sig, edPub, msg, sizeof(msg)));
}
#endif
#endif

#ifdef CRYPTO_TEST_WRAP_DISPATCH
// ---- nRF52840: which backend really ran -----------------------------------------------------------

static unsigned hwCalls, libCalls;

#define WRAP(counter, ret, name, params, args)                                                                                   \
    extern "C" ret __real_##name params;                                                                                         \
    extern "C" ret __wrap_##name params                                                                                          \
    {                                                                                                                            \
        counter++;                                                                                                               \
        return __real_##name args;                                                                                               \
    }
#define CTR_PARAMS (const uint8_t *key, const uint8_t *counter, const uint8_t *in, size_t len, uint8_t *out)
#define CTR_ARGS (key, counter, in, len, out)
#define SEAL_PARAMS                                                                                                              \
    (const uint8_t *key, const uint8_t *nonce, const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len, uint8_t *out,  \
     uint8_t *tag, size_t tag_len)
#define SEAL_ARGS (key, nonce, aad, aad_len, in, len, out, tag, tag_len)
#define OPEN_PARAMS                                                                                                              \
    (const uint8_t *key, const uint8_t *nonce, const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len,                \
     const uint8_t *tag, size_t tag_len, uint8_t *out)
#define OPEN_ARGS (key, nonce, aad, aad_len, in, len, tag, tag_len, out)
#define HASH_PARAMS (const crypto_part *parts, size_t num_parts, uint8_t *out)
#define HASH_ARGS (parts, num_parts, out)
#define HMAC_PARAMS (const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t *out)
#define HMAC_ARGS (key, key_len, parts, num_parts, out)
#define X25519_PARAMS (uint8_t *out, const uint8_t *scalar, const uint8_t *point)
#define X25519_ARGS (out, scalar, point)
#define VERIFY_PARAMS (const uint8_t *signature, const uint8_t *pub, const void *msg, size_t len)
#define VERIFY_ARGS (signature, pub, msg, len)

WRAP(hwCalls, bool, crypto_nrf52_aes128_ctr, CTR_PARAMS, CTR_ARGS)
WRAP(hwCalls, bool, crypto_nrf52_aes128_ccm_seal, SEAL_PARAMS, SEAL_ARGS)
WRAP(hwCalls, bool, crypto_nrf52_aes128_ccm_open, OPEN_PARAMS, OPEN_ARGS)
WRAP(hwCalls, void, crypto_nrf52_sha256, HASH_PARAMS, HASH_ARGS)
WRAP(hwCalls, void, crypto_nrf52_hmac_sha256, HMAC_PARAMS, HMAC_ARGS)
WRAP(hwCalls, bool, crypto_nrf52_x25519, X25519_PARAMS, X25519_ARGS)
WRAP(hwCalls, bool, crypto_nrf52_ed25519_verify, VERIFY_PARAMS, VERIFY_ARGS)
WRAP(hwCalls, bool, crypto_nrf52_ed25519_basemul, (uint8_t *point, const uint8_t *scalar), (point, scalar))
WRAP(libCalls, bool, crypto_lib_aes256_ctr, CTR_PARAMS, CTR_ARGS)
WRAP(libCalls, void, crypto_lib_aes256_ccm_seal, SEAL_PARAMS, SEAL_ARGS)
WRAP(libCalls, bool, crypto_lib_aes256_ccm_open, OPEN_PARAMS, OPEN_ARGS)
WRAP(libCalls, void, crypto_lib_sha256, HASH_PARAMS, HASH_ARGS)
WRAP(libCalls, void, crypto_lib_sha512, HASH_PARAMS, HASH_ARGS)
WRAP(libCalls, void, crypto_lib_hmac_sha256, HMAC_PARAMS, HMAC_ARGS)
WRAP(libCalls, bool, crypto_lib_x25519, X25519_PARAMS, X25519_ARGS)
WRAP(libCalls, bool, crypto_lib_ed25519_verify, VERIFY_PARAMS, VERIFY_ARGS)
WRAP(libCalls, void, crypto_lib_ed25519_basemul, (uint8_t *point, const uint8_t *scalar), (point, scalar))
WRAP(libCalls, void, crypto_lib_xeddsa_sign,
     (uint8_t *signature, const uint8_t *ed_priv, const uint8_t *ed_pub, const void *msg, size_t len, crypto_basemul_fn basemul),
     (signature, ed_priv, ed_pub, msg, len, basemul))

#define EXPECT_BACKEND(primitive, expectHw, expectLib, call)                                                                     \
    do {                                                                                                                         \
        hwCalls = libCalls = 0;                                                                                                  \
        call;                                                                                                                    \
        TEST_MESSAGE("backend " primitive " = " #expectHw " hw, " #expectLib " lib");                                            \
        TEST_ASSERT_EQUAL_UINT_MESSAGE(expectHw, hwCalls, primitive " hardware calls");                                          \
        TEST_ASSERT_EQUAL_UINT_MESSAGE(expectLib, libCalls, primitive " library calls");                                         \
    } while (0)

void test_nrf52_each_public_call_reaches_the_backend_in_the_design_table(void)
{
    uint8_t key[32], data[32] = {0}, out[64], tag[8], sig[64], edPriv[32], edPub[32];
    const crypto_part part = {data, sizeof(data)};
    cv::seqKey(key, sizeof(key));

    EXPECT_BACKEND("aes128-ctr", 1, 0, crypto_aes_ctr(key, 16, cv::kLenCtrBlock, data, 16, out));
    EXPECT_BACKEND("aes256-ctr", 0, 1, crypto_aes_ctr(key, 32, cv::kLenCtrBlock, data, 16, out));
    EXPECT_BACKEND("aes128-ccm-seal", 1, 0, crypto_aes_ccm_seal(key, 16, cv::kLenCcmNonce, nullptr, 0, data, 16, out, tag, 8));
    EXPECT_BACKEND("aes128-ccm-open", 1, 0, crypto_aes_ccm_open(key, 16, cv::kLenCcmNonce, nullptr, 0, out, 16, tag, 8, data));
    EXPECT_BACKEND("aes256-ccm-seal", 0, 1, crypto_aes_ccm_seal(key, 32, cv::kLenCcmNonce, nullptr, 0, data, 16, out, tag, 8));
    EXPECT_BACKEND("aes256-ccm-open", 0, 1, crypto_aes_ccm_open(key, 32, cv::kLenCcmNonce, nullptr, 0, out, 16, tag, 8, data));
    EXPECT_BACKEND("sha-256", 1, 0, crypto_sha256(&part, 1, out));
    EXPECT_BACKEND("sha-512", 0, 1, crypto_sha512(&part, 1, out));
    EXPECT_BACKEND("hmac-sha-256", 1, 0, crypto_hmac_sha256(key, 16, &part, 1, out));
#if !(MESHTASTIC_EXCLUDE_PKI)
    EXPECT_BACKEND("x25519", 1, 0, crypto_x25519(out, key, nullptr));
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    crypto_xeddsa_keys_from_x25519(edPriv, edPub, key);
    EXPECT_BACKEND("xeddsa-sign", 1, 1, crypto_xeddsa_sign(sig, edPriv, edPub, data, sizeof(data)));
    EXPECT_BACKEND("ed25519-verify", 1, 0, crypto_ed25519_verify(sig, edPub, data, sizeof(data)));
    data[0] = 1;
    EXPECT_BACKEND("ed25519-basemul", 1, 0, crypto_ed25519_basemul(out, data));
#endif
#endif
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
    RUN_TEST(test_backend_per_primitive_is_the_one_expected_for_this_target);
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
#ifdef ARCH_ESP32
    RUN_TEST(test_esp32_aes_ctr_matches_library_for_every_key_size_and_length);
    RUN_TEST(test_esp32_aes_ccm_matches_library_for_every_key_size_length_tag_and_aad);
    RUN_TEST(test_esp32_sha_and_hmac_match_library_for_every_length_and_split);
    RUN_TEST(test_esp32_key_and_input_in_flash_give_the_same_results_as_in_ram);
    RUN_TEST(test_esp32_unaligned_in_place_and_hash_inputs_match_library_at_block_boundaries);
    RUN_TEST(test_esp32_psram_buffers_give_the_same_results_as_internal_ram);
    RUN_TEST(test_esp32_concurrent_tasks_get_correct_aes_and_sha_results);
#endif
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
    RUN_TEST(test_aes_ctr_long_lengths_equal_block_by_block_counter);
    RUN_TEST(test_aes_ccm_long_lengths_round_trip_and_reject_tamper);
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_x25519_failure_leaves_out_all_zero);
#endif
#ifdef CRYPTO_HAS_CC310
#ifdef CRYPTO_TEST_WRAP_DISPATCH
    RUN_TEST(test_nrf52_each_public_call_reaches_the_backend_in_the_design_table);
#endif
    RUN_TEST(test_nrf52_aes_ctr_matches_library_for_every_key_size_and_length);
    RUN_TEST(test_nrf52_aes_ccm_matches_library_for_every_key_size_length_tag_and_aad);
    RUN_TEST(test_nrf52_sha_and_hmac_match_library_for_every_length_and_split);
    RUN_TEST(test_nrf52_key_and_input_in_flash_give_the_same_results_as_in_ram);
    RUN_TEST(test_nrf52_unaligned_and_in_place_match_library_at_block_boundaries);
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_nrf52_x25519_matches_library_for_canonical_points_and_top_bit);
    RUN_TEST(test_nrf52_x25519_rejects_exactly_what_the_library_rejects_on_edge_points);
    RUN_TEST(test_nrf52_x25519_scalar_and_point_in_flash);
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    RUN_TEST(test_nrf52_ed25519_verify_rfc8032_through_flash_and_unaligned_buffers);
    RUN_TEST(test_nrf52_ed25519_verify_gives_the_library_verdict_on_edge_keys);
    RUN_TEST(test_nrf52_ed25519_basemul_matches_library_and_refuses_unreduced_scalars);
    RUN_TEST(test_nrf52_xeddsa_signature_with_cc310_R_verifies_in_software);
#endif
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
