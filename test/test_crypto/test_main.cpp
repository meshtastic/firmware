// trunk-ignore-all(gitleaks): These are dummy values. Not real secrets.
#include "CryptoEngine.h"

#include "TestUtil.h"
#include <unity.h>

void HexToBytes(uint8_t *result, const std::string hex, size_t len = 0)
{
    if (len) {
        memset(result, 0, len);
    }
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        result[i / 2] = (uint8_t)strtol(byteString.c_str(), NULL, 16);
    }
    return;
}

void setUp(void)
{
    // set stuff up here
}

void tearDown(void)
{
    // clean stuff up here
}

void test_SHA256(void)
{
    uint8_t expected[32];
    uint8_t hash[32] = {0};

    HexToBytes(expected, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    crypto->hash(hash, 0);
    TEST_ASSERT_EQUAL_MEMORY(hash, expected, 32);

    HexToBytes(hash, "d3", 32);
    HexToBytes(expected, "28969cdfa74a12c82f3bad960b0b000aca2ac329deea5c2328ebc6f2ba9802c1");
    crypto->hash(hash, 1);
    TEST_ASSERT_EQUAL_MEMORY(hash, expected, 32);

    HexToBytes(hash, "11af", 32);
    HexToBytes(expected, "5ca7133fa735326081558ac312c620eeca9970d1e70a4b95533d956f072d1f98");
    crypto->hash(hash, 2);
    TEST_ASSERT_EQUAL_MEMORY(hash, expected, 32);
}
void test_ECB_AES256(void)
{
    // https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_ECB.pdf

    uint8_t key[32] = {0};
    uint8_t plain[16] = {0};
    uint8_t result[16] = {0};
    uint8_t expected[16] = {0};

    HexToBytes(key, "603DEB1015CA71BE2B73AEF0857D77811F352C073B6108D72D9810A30914DFF4");

    HexToBytes(plain, "6BC1BEE22E409F96E93D7E117393172A");
    HexToBytes(expected, "F3EED1BDB5D2A03C064B5A7E3DB181F8");
    crypto->aesSetKey(key, 32);
    crypto->aesEncrypt(plain, result); // Does 16 bytes at a time
    TEST_ASSERT_EQUAL_MEMORY(expected, result, 16);

    HexToBytes(plain, "AE2D8A571E03AC9C9EB76FAC45AF8E51");
    HexToBytes(expected, "591CCB10D410ED26DC5BA74A31362870");
    crypto->aesSetKey(key, 32);
    crypto->aesEncrypt(plain, result); // Does 16 bytes at a time
    TEST_ASSERT_EQUAL_MEMORY(expected, result, 16);

    HexToBytes(plain, "30C81C46A35CE411E5FBC1191A0A52EF");
    HexToBytes(expected, "B6ED21B99CA6F4F9F153E7B1BEAFED1D");
    crypto->aesSetKey(key, 32);
    crypto->aesEncrypt(plain, result); // Does 16 bytes at a time
    TEST_ASSERT_EQUAL_MEMORY(expected, result, 16);
}
void test_DH25519(void)
{
    // test vectors from wycheproof x25519
    // https://github.com/C2SP/wycheproof/blob/master/testvectors/x25519_test.json
    uint8_t private_key[32];
    uint8_t public_key[32];
    uint8_t expected_shared[32];

    HexToBytes(public_key, "504a36999f489cd2fdbc08baff3d88fa00569ba986cba22548ffde80f9806829");
    HexToBytes(private_key, "c8a9d5a91091ad851c668b0736c1c9a02936c0d3ad62670858088047ba057475");
    HexToBytes(expected_shared, "436a2c040cf45fea9b29a0cb81b1f41458f863d0d61b453d0a982720d6d61320");
    crypto->setDHPrivateKey(private_key);
    TEST_ASSERT(crypto->setDHPublicKey(public_key));
    TEST_ASSERT_EQUAL_MEMORY(expected_shared, crypto->shared_key, 32);

    HexToBytes(public_key, "63aa40c6e38346c5caf23a6df0a5e6c80889a08647e551b3563449befcfc9733");
    HexToBytes(private_key, "d85d8c061a50804ac488ad774ac716c3f5ba714b2712e048491379a500211958");
    HexToBytes(expected_shared, "279df67a7c4611db4708a0e8282b195e5ac0ed6f4b2f292c6fbd0acac30d1332");
    crypto->setDHPrivateKey(private_key);
    TEST_ASSERT(crypto->setDHPublicKey(public_key));
    TEST_ASSERT_EQUAL_MEMORY(expected_shared, crypto->shared_key, 32);

    HexToBytes(public_key, "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f");
    HexToBytes(private_key, "18630f93598637c35da623a74559cf944374a559114c7937811041fc8605564a");
    crypto->setDHPrivateKey(private_key);
    TEST_ASSERT(!crypto->setDHPublicKey(public_key)); // Weak public key results in 0 shared key

    HexToBytes(public_key, "f7e13a1a067d2f4e1061bf9936fde5be6b0c2494a8f809cbac7f290ef719e91c");
    HexToBytes(private_key, "10300724f3bea134eb1575245ef26ff9b8ccd59849cd98ce1a59002fe1d5986c");
    HexToBytes(expected_shared, "24becd5dfed9e9289ba2e15b82b0d54f8e9aacb72f5e4248c58d8d74b451ce76");
    crypto->setDHPrivateKey(private_key);
    TEST_ASSERT(crypto->setDHPublicKey(public_key));
    crypto->hash(crypto->shared_key, 32);
    TEST_ASSERT_EQUAL_MEMORY(expected_shared, crypto->shared_key, 32);
}

void test_PKC(void)
{
    uint8_t private_key[32];
    meshtastic_UserLite_public_key_t public_key;
    uint8_t expected_shared[32];
    uint8_t expected_decrypted[32];
    uint8_t radioBytes[128] __attribute__((__aligned__));
    uint8_t decrypted[128] __attribute__((__aligned__));
    uint8_t expected_nonce[16];

    uint32_t fromNode = 0x0929;
    uint64_t packetNum = 0x13b2d662;
    HexToBytes(public_key.bytes, "db18fc50eea47f00251cb784819a3cf5fc361882597f589f0d7ff820e8064457");
    public_key.size = 32;
    HexToBytes(private_key, "a00330633e63522f8a4d81ec6d9d1e6617f6c8ffd3a4c698229537d44e522277");
    HexToBytes(expected_shared, "777b1545c9d6f9a2");
    HexToBytes(expected_decrypted, "08011204746573744800");
    HexToBytes(radioBytes, "8c646d7a2909000062d6b2136b00000040df24abfcc30a17a3d9046726099e796a1c036a792b");
    HexToBytes(expected_nonce, "62d6b213036a792b2909000000");
    crypto->setDHPrivateKey(private_key);

    TEST_ASSERT(crypto->decryptCurve25519(fromNode, public_key, packetNum, 22, radioBytes + 16, decrypted));
    TEST_ASSERT_EQUAL_MEMORY(expected_shared, crypto->shared_key, 8);
    TEST_ASSERT_EQUAL_MEMORY(expected_nonce, crypto->nonce, 13);
    TEST_ASSERT_EQUAL_MEMORY(expected_decrypted, decrypted, 10);

    uint32_t toNode = 0; // Only impacts logging
    uint8_t encrypted[128] __attribute__((__aligned__));
    TEST_ASSERT(crypto->encryptCurve25519(toNode, fromNode, public_key, packetNum, 10, decrypted, encrypted));
    TEST_ASSERT_EQUAL_MEMORY(expected_shared, crypto->shared_key, 8);
    // The extraNonce is random, so skip checking the nonce and encrypted output here

    // Copy the nonce to check it after encryption
    memcpy(expected_nonce, crypto->nonce, 16);

    // Decrypt the re-encrypted bytes and check they are the same as what we expect
    TEST_ASSERT(crypto->decryptCurve25519(fromNode, public_key, packetNum, 22, encrypted, decrypted));
    TEST_ASSERT_EQUAL_MEMORY(expected_shared, crypto->shared_key, 8);
    TEST_ASSERT_EQUAL_MEMORY(expected_nonce, crypto->nonce, 13);
    TEST_ASSERT_EQUAL_MEMORY(expected_decrypted, decrypted, 10);
}

void test_ECB_AES128(void)
{
    // Verify aesSetKey fix: 16-byte keys now use AESSmall128 instead of AESSmall256
    // NIST SP 800-38A, Appendix F.1 (ECB-AES128-Encrypt)
    uint8_t key[16] = {0};
    uint8_t plain[16] = {0};
    uint8_t result[16] = {0};
    uint8_t expected[16] = {0};

    HexToBytes(key, "2B7E151628AED2A6ABF7158809CF4F3C");

    HexToBytes(plain, "6BC1BEE22E409F96E93D7E117393172A");
    HexToBytes(expected, "3AD77BB40D7A3660A89ECAF32466EF97");
    crypto->aesSetKey(key, 16);
    crypto->aesEncrypt(plain, result);
    TEST_ASSERT_EQUAL_MEMORY(expected, result, 16);

    HexToBytes(plain, "AE2D8A571E03AC9C9EB76FAC45AF8E51");
    HexToBytes(expected, "F5D3D58503B9699DE785895A96FDBAAF");
    crypto->aesSetKey(key, 16);
    crypto->aesEncrypt(plain, result);
    TEST_ASSERT_EQUAL_MEMORY(expected, result, 16);
}

void test_CCM_channel_roundtrip_200bytes(void)
{
    // Round-trip AES-CCM on a 200-byte payload (typical large text message)
    uint8_t plain[256] __attribute__((__aligned__));
    uint8_t encrypted[256] __attribute__((__aligned__));
    uint8_t decrypted[256] __attribute__((__aligned__));
    CryptoKey k;

    // Default Meshtastic PSK (16 bytes, AES-128)
    HexToBytes(k.bytes, "d4f1bb3a20290759f0bcffabcf4e6901");
    k.length = 16;
    crypto->setKey(k);

    uint32_t fromNode = 0xDEADBEEF;
    uint64_t packetId = 0x12345678;

    // Fill with recognizable pattern
    for (int i = 0; i < 200; i++)
        plain[i] = (uint8_t)(i & 0xFF);

    // Encrypt
    int ret = crypto->encryptPacketCCM(fromNode, packetId, 200, plain, encrypted);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Ciphertext must differ from plaintext
    TEST_ASSERT_FALSE(memcmp(encrypted, plain, 200) == 0);

    // Decrypt and verify round-trip
    bool ok = crypto->decryptPacketCCM(fromNode, packetId, 200 + MESHTASTIC_CCM_TAG_SIZE, encrypted, decrypted);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, 200);
}

void test_CCM_bit_corruption_rejects(void)
{
    // A single flipped bit in ciphertext must cause CCM auth verification to fail
    uint8_t plain[32] __attribute__((__aligned__));
    uint8_t encrypted[48] __attribute__((__aligned__));
    uint8_t decrypted[32] __attribute__((__aligned__));
    CryptoKey k;

    HexToBytes(k.bytes, "d4f1bb3a20290759f0bcffabcf4e6901");
    k.length = 16;
    crypto->setKey(k);

    uint32_t fromNode = 0x1234;
    uint64_t packetId = 0xABCD;
    memset(plain, 0x42, 20);

    int ret = crypto->encryptPacketCCM(fromNode, packetId, 20, plain, encrypted);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Flip one bit in the ciphertext body (not the tag)
    encrypted[10] ^= 0x01;

    bool ok = crypto->decryptPacketCCM(fromNode, packetId, 20 + MESHTASTIC_CCM_TAG_SIZE, encrypted, decrypted);
    TEST_ASSERT_FALSE(ok);
}

void test_CTR_legacy_roundtrip(void)
{
    // Legacy AES-CTR path: encrypt then decrypt must recover plaintext
    uint8_t plain[32] __attribute__((__aligned__));
    uint8_t work[32] __attribute__((__aligned__));
    CryptoKey k;

    HexToBytes(k.bytes, "d4f1bb3a20290759f0bcffabcf4e6901");
    k.length = 16;
    crypto->setKey(k);

    uint32_t fromNode = 0x5678;
    uint64_t packetId = 0x9ABC;

    memcpy(plain, "Hello from legacy node!!", 24);
    memcpy(work, plain, 24);

    // Encrypt with CTR (legacy path)
    crypto->encryptPacket(fromNode, packetId, 24, work);
    TEST_ASSERT_FALSE(memcmp(work, plain, 24) == 0);

    // Decrypt with CTR (symmetric)
    crypto->decrypt(fromNode, packetId, 24, work);
    TEST_ASSERT_EQUAL_MEMORY(plain, work, 24);
}

void test_CCM_wrong_tag_rejects(void)
{
    // Corrupted auth tag must cause decryption to fail
    uint8_t plain[32] __attribute__((__aligned__));
    uint8_t encrypted[48] __attribute__((__aligned__));
    uint8_t decrypted[32] __attribute__((__aligned__));
    CryptoKey k;

    HexToBytes(k.bytes, "d4f1bb3a20290759f0bcffabcf4e6901");
    k.length = 16;
    crypto->setKey(k);

    uint32_t fromNode = 0x1111;
    uint64_t packetId = 0x2222;
    memset(plain, 0xAA, 16);

    int ret = crypto->encryptPacketCCM(fromNode, packetId, 16, plain, encrypted);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Corrupt the auth tag (bytes 16-23)
    encrypted[16] ^= 0xFF;
    encrypted[17] ^= 0xFF;

    bool ok = crypto->decryptPacketCCM(fromNode, packetId, 16 + MESHTASTIC_CCM_TAG_SIZE, encrypted, decrypted);
    TEST_ASSERT_FALSE(ok);
}

void test_CCM_oversized_ctr_fallback(void)
{
    // Verify both CCM and CTR work at boundary sizes
    // Router uses CCM when: numbytes + 16 header + 8 tag <= 255
    // So max CCM plaintext = 255 - 16 - 8 = 231 bytes
    uint8_t plain[256] __attribute__((__aligned__));
    uint8_t encrypted[256] __attribute__((__aligned__));
    uint8_t decrypted[256] __attribute__((__aligned__));
    uint8_t work[256] __attribute__((__aligned__));
    CryptoKey k;

    HexToBytes(k.bytes, "d4f1bb3a20290759f0bcffabcf4e6901");
    k.length = 16;
    crypto->setKey(k);

    uint32_t fromNode = 0x3333;
    uint64_t packetId = 0x4444;

    for (int i = 0; i < 239; i++)
        plain[i] = (uint8_t)(i & 0xFF);

    // 231 bytes: max size that fits CCM in LoRa frame (231 + 8 + 16 = 255)
    int ret = crypto->encryptPacketCCM(fromNode, packetId, 231, plain, encrypted);
    TEST_ASSERT_EQUAL_INT(0, ret);
    bool ok = crypto->decryptPacketCCM(fromNode, packetId, 231 + MESHTASTIC_CCM_TAG_SIZE, encrypted, decrypted);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, 231);

    // 239 bytes: too large for CCM in LoRa frame, Router would use CTR
    // CTR must still work at max payload size
    memcpy(work, plain, 239);
    crypto->encryptPacket(fromNode, packetId, 239, work);
    crypto->decrypt(fromNode, packetId, 239, work);
    TEST_ASSERT_EQUAL_MEMORY(plain, work, 239);
}

void test_CCM_both_key_sizes(void)
{
    // Verify CCM works with both 16-byte (AES-128) and 32-byte (AES-256) PSKs
    uint8_t plain[32] __attribute__((__aligned__));
    uint8_t encrypted128[48] __attribute__((__aligned__));
    uint8_t encrypted256[48] __attribute__((__aligned__));
    uint8_t decrypted[32] __attribute__((__aligned__));
    CryptoKey k;

    uint32_t fromNode = 0x7777;
    uint64_t packetId = 0x8888;
    memcpy(plain, "Test both key sizes!", 20);

    // --- AES-128 (16-byte PSK, default Meshtastic) ---
    HexToBytes(k.bytes, "d4f1bb3a20290759f0bcffabcf4e6901");
    k.length = 16;
    crypto->setKey(k);

    int ret = crypto->encryptPacketCCM(fromNode, packetId, 20, plain, encrypted128);
    TEST_ASSERT_EQUAL_INT(0, ret);
    bool ok = crypto->decryptPacketCCM(fromNode, packetId, 20 + MESHTASTIC_CCM_TAG_SIZE, encrypted128, decrypted);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, 20);

    // --- AES-256 (32-byte PSK) ---
    HexToBytes(k.bytes, "603DEB1015CA71BE2B73AEF0857D77811F352C073B6108D72D9810A30914DFF4");
    k.length = 32;
    crypto->setKey(k);

    memset(decrypted, 0, sizeof(decrypted));
    ret = crypto->encryptPacketCCM(fromNode, packetId, 20, plain, encrypted256);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ok = crypto->decryptPacketCCM(fromNode, packetId, 20 + MESHTASTIC_CCM_TAG_SIZE, encrypted256, decrypted);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, 20);

    // AES-128 and AES-256 must produce different ciphertext+tag for same plaintext
    TEST_ASSERT_FALSE(memcmp(encrypted128, encrypted256, 20 + MESHTASTIC_CCM_TAG_SIZE) == 0);
}

void test_AES_CTR(void)
{
    uint8_t expected[32];
    uint8_t plain[32];
    uint8_t nonce[32];
    CryptoKey k;

    // vectors from https://www.rfc-editor.org/rfc/rfc3686#section-6
    k.length = 32;
    HexToBytes(k.bytes, "776BEFF2851DB06F4C8A0542C8696F6C6A81AF1EEC96B4D37FC1D689E6C1C104");
    HexToBytes(nonce, "00000060DB5672C97AA8F0B200000001");
    HexToBytes(expected, "145AD01DBF824EC7560863DC71E3E0C0");
    memcpy(plain, "Single block msg", 16);

    crypto->encryptAESCtr(k, nonce, 16, plain);
    TEST_ASSERT_EQUAL_MEMORY(expected, plain, 16);

    k.length = 16;
    memcpy(plain, "Single block msg", 16);
    HexToBytes(k.bytes, "AE6852F8121067CC4BF7A5765577F39E");
    HexToBytes(nonce, "00000030000000000000000000000001");
    HexToBytes(expected, "E4095D4FB7A7B3792D6175A3261311B8");
    crypto->encryptAESCtr(k, nonce, 16, plain);
    TEST_ASSERT_EQUAL_MEMORY(expected, plain, 16);
}

void setup()
{
    // NOTE!!! Wait for >2 secs
    // if board doesn't support software reset via Serial.DTR/RTS
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    UNITY_BEGIN(); // IMPORTANT LINE!
    RUN_TEST(test_SHA256);
    RUN_TEST(test_ECB_AES256);
    RUN_TEST(test_ECB_AES128);
    RUN_TEST(test_DH25519);
    RUN_TEST(test_AES_CTR);
    RUN_TEST(test_PKC);
    RUN_TEST(test_CCM_channel_roundtrip_200bytes);
    RUN_TEST(test_CCM_bit_corruption_rejects);
    RUN_TEST(test_CTR_legacy_roundtrip);
    RUN_TEST(test_CCM_wrong_tag_rejects);
    RUN_TEST(test_CCM_oversized_ctr_fallback);
    RUN_TEST(test_CCM_both_key_sizes);
    exit(UNITY_END()); // stop unit testing
}

void loop() {}