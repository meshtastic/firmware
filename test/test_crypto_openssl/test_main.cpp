// Differential test of the functional crypto API in src/crypto/crypto_api.h against the system
// OpenSSL (libcrypto): AES-128/256 CTR, AES-CCM with 8 and 12 byte tags, SHA-256/512, HMAC-SHA256,
// X25519 and Ed25519 verify, on seeded random inputs.
//
// Why: the known-answer vectors pin a few inputs; a replacement backend can still be wrong on
// lengths, alignments or key values nobody listed. OpenSSL is an independent implementation, so any
// disagreement is a bug in one of them. Native only: it links libcrypto.
//
// Regressions guarded: any backend diverging from OpenSSL on ciphertext, tag, digest, shared secret
// or verify result. A failure prints the seed; rerun with CRYPTO_DIFF_SEED=<seed> to reproduce.
#include "configuration.h"

#include "crypto/crypto_api.h"
#include <Arduino.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

static const int kIterations = 200;
static uint64_t seed;
static uint64_t rngState;
static char where[96];

void setUp(void) {}
void tearDown(void) {}

static uint64_t next()
{
    uint64_t z = (rngState += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void fill(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)next();
}

static void at(const char *what, int i)
{
    snprintf(where, sizeof(where), "%s iteration %d seed %llu", what, i, (unsigned long long)seed);
}

static void begin(uint64_t offset)
{
    rngState = seed + offset;
}

static bool osslCtr(const uint8_t *key, size_t keyLen, const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out)
{
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int n = 0, m = 0;
    bool ok = EVP_EncryptInit_ex(c, keyLen == 16 ? EVP_aes_128_ctr() : EVP_aes_256_ctr(), nullptr, key, iv) &&
              EVP_EncryptUpdate(c, out, &n, in, (int)len) && EVP_EncryptFinal_ex(c, out + n, &m);
    EVP_CIPHER_CTX_free(c);
    return ok;
}

static bool osslCcmSeal(const uint8_t *key, size_t keyLen, const uint8_t nonce[13], const uint8_t *aad, size_t aadLen,
                        const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tagLen)
{
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int n = 0;
    bool ok = EVP_EncryptInit_ex(c, keyLen == 16 ? EVP_aes_128_ccm() : EVP_aes_256_ccm(), nullptr, nullptr, nullptr) &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_IVLEN, 13, nullptr) &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_TAG, (int)tagLen, nullptr) &&
              EVP_EncryptInit_ex(c, nullptr, nullptr, key, nonce) && EVP_EncryptUpdate(c, nullptr, &n, nullptr, (int)len) &&
              (aadLen == 0 || EVP_EncryptUpdate(c, nullptr, &n, aad, (int)aadLen)) &&
              EVP_EncryptUpdate(c, out, &n, in, (int)len) && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_GET_TAG, (int)tagLen, tag);
    EVP_CIPHER_CTX_free(c);
    return ok;
}

static bool osslDigest(const EVP_MD *md, const uint8_t *in, size_t len, uint8_t *out)
{
    unsigned n = 0;
    return EVP_Digest(in, len, out, &n, md, nullptr);
}

static bool osslHmac256(const uint8_t *key, size_t keyLen, const uint8_t *in, size_t len, uint8_t *out)
{
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX *c = EVP_MAC_CTX_new(mac);
    OSSL_PARAM params[2] = {OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, (char *)"SHA256", 0),
                            OSSL_PARAM_construct_end()};
    size_t n = 0;
    bool ok = EVP_MAC_init(c, key, keyLen, params) && EVP_MAC_update(c, in, len) && EVP_MAC_final(c, out, &n, 32);
    EVP_MAC_CTX_free(c);
    EVP_MAC_free(mac);
    return ok;
}

static bool osslX25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    EVP_PKEY *priv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, scalar, 32);
    EVP_PKEY *pub = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, point, 32);
    EVP_PKEY_CTX *c = priv ? EVP_PKEY_CTX_new(priv, nullptr) : nullptr;
    size_t n = 32;
    bool ok = c && pub && EVP_PKEY_derive_init(c) > 0 && EVP_PKEY_derive_set_peer(c, pub) > 0 && EVP_PKEY_derive(c, out, &n) > 0;
    EVP_PKEY_CTX_free(c);
    EVP_PKEY_free(pub);
    EVP_PKEY_free(priv);
    return ok;
}

static bool osslEdSign(uint8_t sig[64], uint8_t pub[32], const uint8_t priv[32], const uint8_t *msg, size_t len)
{
    EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv, 32);
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    size_t sigLen = 64, pubLen = 32;
    bool ok = k && EVP_PKEY_get_raw_public_key(k, pub, &pubLen) && EVP_DigestSignInit(c, nullptr, nullptr, nullptr, k) &&
              EVP_DigestSign(c, sig, &sigLen, msg, len);
    EVP_MD_CTX_free(c);
    EVP_PKEY_free(k);
    return ok;
}

static bool osslEdVerify(const uint8_t sig[64], const uint8_t pub[32], const uint8_t *msg, size_t len)
{
    EVP_PKEY *k = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pub, 32);
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    bool ok = k && EVP_DigestVerifyInit(c, nullptr, nullptr, nullptr, k) && EVP_DigestVerify(c, sig, 64, msg, len) == 1;
    EVP_MD_CTX_free(c);
    EVP_PKEY_free(k);
    return ok;
}

void test_aes_ctr_matches_openssl_for_random_keys_counters_and_lengths(void)
{
    begin(1);
    static uint8_t in[520], a[520], b[520];
    for (int i = 0; i < kIterations; i++) {
        size_t keyLen = (next() & 1) ? 16 : 32, len = next() % 513;
        uint8_t key[32], ctr[16];
        fill(key, 32);
        fill(ctr, 16);
        fill(in, sizeof(in));
        at("aes_ctr", i);
        TEST_ASSERT_TRUE_MESSAGE(osslCtr(key, keyLen, ctr, in, len, a), where);
        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ctr(key, keyLen, ctr, in, len, b), where);
        if (len)
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, len, where);
    }
}

void test_aes_ccm_matches_openssl_with_8_and_12_byte_tags(void)
{
    begin(2);
    static uint8_t in[520], a[520], b[520], opened[520];
    for (int i = 0; i < kIterations; i++) {
        size_t keyLen = (next() & 1) ? 16 : 32, len = next() % 513, aadLen = next() % 31, tagLen = (next() & 1) ? 8 : 12;
        uint8_t key[32], nonce[13], aad[30], tagA[16], tagB[16];
        fill(key, 32);
        fill(nonce, 13);
        fill(aad, 30);
        fill(in, sizeof(in));
        at("aes_ccm", i);
        TEST_ASSERT_TRUE_MESSAGE(osslCcmSeal(key, keyLen, nonce, aad, aadLen, in, len, a, tagA, tagLen), where);
        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ccm_seal(key, keyLen, nonce, aad, aadLen, in, len, b, tagB, tagLen), where);
        if (len)
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, len, where);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(tagA, tagB, tagLen, where);
        TEST_ASSERT_TRUE_MESSAGE(crypto_aes_ccm_open(key, keyLen, nonce, aad, aadLen, a, len, tagA, tagLen, opened), where);
        if (len)
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(in, opened, len, where);
    }
}

void test_sha256_sha512_and_hmac_match_openssl_over_random_split_parts(void)
{
    begin(3);
    static uint8_t in[600];
    for (int i = 0; i < kIterations; i++) {
        size_t len = next() % 601, cut = len ? next() % (len + 1) : 0, keyLen = next() % 100;
        uint8_t key[100], a[64], b[64];
        fill(key, sizeof(key));
        fill(in, sizeof(in));
        const crypto_part parts[2] = {{in, cut}, {in + cut, len - cut}};
        at("sha256", i);
        TEST_ASSERT_TRUE_MESSAGE(osslDigest(EVP_sha256(), in, len, a), where);
        crypto_sha256(parts, 2, b);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, 32, where);
        at("sha512", i);
        TEST_ASSERT_TRUE_MESSAGE(osslDigest(EVP_sha512(), in, len, a), where);
        crypto_sha512(parts, 2, b);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, 64, where);
        at("hmac", i);
        TEST_ASSERT_TRUE_MESSAGE(osslHmac256(key, keyLen, in, len, a), where);
        crypto_hmac_sha256(key, keyLen, parts, 2, b);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, 32, where);
    }
}

void test_x25519_matches_openssl_for_random_scalars_and_points(void)
{
    begin(4);
    for (int i = 0; i < kIterations; i++) {
        uint8_t scalar[32], point[32], a[32], b[32];
        fill(scalar, 32);
        fill(point, 32);
        point[31] &= 0x7f;
        at("x25519", i);
        TEST_ASSERT_TRUE_MESSAGE(osslX25519(a, scalar, point), where);
        TEST_ASSERT_TRUE_MESSAGE(crypto_x25519(b, scalar, point), where);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, 32, where);
    }
}

void test_x25519_public_key_matches_openssl_from_the_base_point(void)
{
    begin(5);
    static const uint8_t base[32] = {9};
    for (int i = 0; i < kIterations; i++) {
        uint8_t scalar[32], a[32], b[32];
        fill(scalar, 32);
        at("x25519_base", i);
        TEST_ASSERT_TRUE_MESSAGE(osslX25519(a, scalar, base), where);
        TEST_ASSERT_TRUE_MESSAGE(crypto_x25519(b, scalar, nullptr), where);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, 32, where);
    }
}

void test_ed25519_verify_agrees_with_openssl_on_valid_and_mutated_signatures(void)
{
    begin(6);
    static uint8_t msg[200];
    for (int i = 0; i < kIterations; i++) {
        uint8_t priv[32], pub[32], sig[64];
        size_t len = next() % 201;
        fill(priv, 32);
        fill(msg, sizeof(msg));
        at("ed25519_verify", i);
        TEST_ASSERT_TRUE_MESSAGE(osslEdSign(sig, pub, priv, msg, len), where);
        TEST_ASSERT_TRUE_MESSAGE(crypto_ed25519_verify(sig, pub, msg, len), where);

        uint8_t bad[64];
        memcpy(bad, sig, 64);
        bad[next() % 64] ^= (uint8_t)(1u << (next() % 8));
        TEST_ASSERT_EQUAL_MESSAGE(osslEdVerify(bad, pub, msg, len), crypto_ed25519_verify(bad, pub, msg, len), where);
        TEST_ASSERT_FALSE_MESSAGE(crypto_ed25519_verify(bad, pub, msg, len), where);
    }
}

void setup()
{
    delay(10);
    const char *env = getenv("CRYPTO_DIFF_SEED");
    seed = env ? strtoull(env, nullptr, 10) : 20260101;
    printf("crypto differential seed %llu\n", (unsigned long long)seed);
    UNITY_BEGIN();
    RUN_TEST(test_aes_ctr_matches_openssl_for_random_keys_counters_and_lengths);
    RUN_TEST(test_aes_ccm_matches_openssl_with_8_and_12_byte_tags);
    RUN_TEST(test_sha256_sha512_and_hmac_match_openssl_over_random_split_parts);
    RUN_TEST(test_x25519_matches_openssl_for_random_scalars_and_points);
    RUN_TEST(test_x25519_public_key_matches_openssl_from_the_base_point);
    RUN_TEST(test_ed25519_verify_agrees_with_openssl_on_valid_and_mutated_signatures);
    exit(UNITY_END());
}

void loop() {}
