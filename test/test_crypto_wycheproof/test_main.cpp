// Project Wycheproof vectors (test/support/wycheproof_vectors.h) run through the functional crypto
// API in src/crypto/crypto_api.h: crypto_aes_ccm_seal/open, crypto_x25519 and crypto_ed25519_verify.
//
// Why: the RFC vectors cover well-formed inputs. Wycheproof adds the hostile ones (low-order and
// non-canonical X25519 points, malleable or truncated Ed25519 signatures, corrupted CCM tags), which
// is where a replacement backend most likely accepts something it must reject. Expected results come
// from the Wycheproof files, not from the code under test.
//
// Regressions guarded: a backend accepting a forged signature or tampered CCM message, or computing
// a wrong X25519 shared secret. Vectors Wycheproof calls "acceptable" may go either way but must
// never produce a wrong value. Where today's behaviour differs from the expected result the vector
// is listed in kKnown with the reason, and the test fails if that list and the observed differences
// ever disagree, so a fix or a regression both show up.
#include "configuration.h"

#include "crypto/crypto_api.h"
#include "support/wycheproof_vectors.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

namespace wp = wycheproof;

struct Known {
    char suite;
    int id;
    const char *reason;
};

static const Known kKnown[] = {};

static size_t unhex(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = strlen(hex) / 2;
    TEST_ASSERT_TRUE(n <= cap);
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
    return n;
}

static bool isKnown(char suite, int id)
{
    for (const Known &k : kKnown)
        if (k.suite == suite && k.id == id)
            return true;
    return false;
}

// Differences are collected so one run reports all of them, then compared with kKnown.
static int observed[512];
static char observedSuite[512];
static int numObserved;

static void differs(char suite, int id, const char *what)
{
    printf("wycheproof %c tcId %d: %s\n", suite, id, what);
    if (numObserved < 512) {
        observed[numObserved] = id;
        observedSuite[numObserved++] = suite;
    }
}

static void checkObservedMatchesKnown(char suite)
{
    int count = 0;
    for (int i = 0; i < numObserved; i++) {
        if (observedSuite[i] != suite)
            continue;
        count++;
        TEST_ASSERT_TRUE_MESSAGE(isKnown(suite, observed[i]), "difference from Wycheproof not listed in kKnown");
    }
    int listed = 0;
    for (const Known &k : kKnown)
        listed += k.suite == suite;
    TEST_ASSERT_EQUAL_MESSAGE(listed, count, "kKnown lists a vector that now behaves as Wycheproof expects");
}

void setUp(void)
{
    numObserved = 0;
}

void tearDown(void) {}

void test_aes_ccm_wycheproof_vectors(void)
{
    uint8_t key[32], nonce[13], aad[64], msg[64], ct[64], tag[16], out[64], sealedTag[16];
    for (const wp::Ccm &v : wp::kCcm) {
        size_t keyLen = unhex(v.key, key, sizeof(key));
        unhex(v.nonce, nonce, sizeof(nonce));
        size_t aadLen = unhex(v.aad, aad, sizeof(aad));
        size_t len = unhex(v.msg, msg, sizeof(msg));
        size_t ctLen = unhex(v.ct, ct, sizeof(ct));
        size_t tagLen = unhex(v.tag, tag, sizeof(tag));
        if (aadLen > 30) {
            differs('c', v.id, "aad over 30 bytes is outside the API");
            continue;
        }
        bool opened = ctLen == len && crypto_aes_ccm_open(key, keyLen, nonce, aad, aadLen, ct, ctLen, tag, tagLen, out);
        if (v.expect == wp::WP_VALID) {
            if (!opened || (len && memcmp(out, msg, len)))
                differs('c', v.id, "valid vector not opened");
            else if (!crypto_aes_ccm_seal(key, keyLen, nonce, aad, aadLen, msg, len, out, sealedTag, tagLen) ||
                     (len && memcmp(out, ct, len)) || memcmp(sealedTag, tag, tagLen))
                differs('c', v.id, "valid vector seals to a different message");
        } else if (v.expect == wp::WP_INVALID && opened) {
            differs('c', v.id, "invalid vector opened");
        }
    }
    checkObservedMatchesKnown('c');
}

void test_x25519_wycheproof_vectors(void)
{
    uint8_t priv[32], pub[32], shared[32], out[32];
    for (const wp::X25519 &v : wp::kX25519) {
        unhex(v.priv, priv, sizeof(priv));
        unhex(v.pub, pub, sizeof(pub));
        unhex(v.shared, shared, sizeof(shared));
        bool ok = crypto_x25519(out, priv, pub);
        if (v.expect == wp::WP_VALID && !ok)
            differs('x', v.id, "valid vector rejected");
        else if (ok && memcmp(out, shared, 32))
            differs('x', v.id, "wrong shared secret");
    }
    checkObservedMatchesKnown('x');
}

void test_ed25519_verify_wycheproof_vectors(void)
{
    uint8_t pub[32], msg[1024], sig[200];
    for (const wp::Ed25519 &v : wp::kEd25519) {
        unhex(v.pub, pub, sizeof(pub));
        size_t len = unhex(v.msg, msg, sizeof(msg));
        size_t sigLen = unhex(v.sig, sig, sizeof(sig));
        bool ok = sigLen == 64 && crypto_ed25519_verify(sig, pub, msg, len);
        if (v.expect == wp::WP_VALID && !ok)
            differs('e', v.id, "valid signature rejected");
        else if (v.expect == wp::WP_INVALID && ok)
            differs('e', v.id, "invalid signature accepted");
    }
    checkObservedMatchesKnown('e');
}

void setup()
{
    delay(10);
    UNITY_BEGIN();
    RUN_TEST(test_aes_ccm_wycheproof_vectors);
    RUN_TEST(test_x25519_wycheproof_vectors);
    RUN_TEST(test_ed25519_verify_wycheproof_vectors);
    exit(UNITY_END());
}

void loop() {}
