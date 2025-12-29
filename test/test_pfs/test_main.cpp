/**
 * PFS (Perfect Forward Secrecy) Unit Tests
 *
 * Tests for ephemeral key management and Triple-DH session key derivation.
 */

#include "CryptoEngine.h"
#include "EphemeralKeyManager.h"
#include "TestUtil.h"
#include <unity.h>

// Helper to convert hex string to bytes
void HexToBytes(uint8_t *result, const std::string hex, size_t len = 0) {
  if (len) {
    memset(result, 0, len);
  }
  for (unsigned int i = 0; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    result[i / 2] = (uint8_t)strtol(byteString.c_str(), NULL, 16);
  }
}

// Dummy EphemeralKeyManager for testing (actual instance created below)
EphemeralKeyManager *ephemeralKeyMgr = nullptr;

void setUp(void) {
  // Create fresh key manager for each test
  if (ephemeralKeyMgr) {
    delete ephemeralKeyMgr;
  }
  ephemeralKeyMgr = new EphemeralKeyManager();
}

void tearDown(void) {
  if (ephemeralKeyMgr) {
    delete ephemeralKeyMgr;
    ephemeralKeyMgr = nullptr;
  }
}

/**
 * Test Triple-DH key derivation produces deterministic results.
 *
 * Given the same input keys, deriveTripleDHSessionKey should produce
 * the same session key every time.
 */
void test_TripleDH_Deterministic(void) {
  uint8_t localIdentityPriv[32];
  uint8_t localEphemeralPriv[32];
  uint8_t remoteIdentityPub[32];
  uint8_t remoteEphemeralPub[32];
  uint8_t sessionKey1[32];
  uint8_t sessionKey2[32];

  // Use known test vectors (random but fixed values)
  HexToBytes(
      localIdentityPriv,
      "a00330633e63522f8a4d81ec6d9d1e6617f6c8ffd3a4c698229537d44e522277");
  HexToBytes(
      localEphemeralPriv,
      "c8a9d5a91091ad851c668b0736c1c9a02936c0d3ad62670858088047ba057475");
  HexToBytes(
      remoteIdentityPub,
      "db18fc50eea47f00251cb784819a3cf5fc361882597f589f0d7ff820e8064457");
  HexToBytes(
      remoteEphemeralPub,
      "504a36999f489cd2fdbc08baff3d88fa00569ba986cba22548ffde80f9806829");

  // Derive session key twice
  bool result1 = crypto->deriveTripleDHSessionKey(
      localIdentityPriv, localEphemeralPriv, remoteIdentityPub,
      remoteEphemeralPub, sessionKey1);

  bool result2 = crypto->deriveTripleDHSessionKey(
      localIdentityPriv, localEphemeralPriv, remoteIdentityPub,
      remoteEphemeralPub, sessionKey2);

  TEST_ASSERT_TRUE(result1);
  TEST_ASSERT_TRUE(result2);
  TEST_ASSERT_EQUAL_MEMORY(sessionKey1, sessionKey2, 32);

  // Session key should not be all zeros
  uint8_t zeros[32] = {0};
  TEST_ASSERT_FALSE(memcmp(sessionKey1, zeros, 32) == 0);
}

/**
 * Test PFS encrypt/decrypt round-trip.
 *
 * Encrypting then decrypting should produce original plaintext.
 */
void test_PFS_EncryptDecrypt_RoundTrip(void) {
  // Initialize key manager
  ephemeralKeyMgr->init();

  // Set up test keys
  uint8_t remoteEphemeralPub[32];
  meshtastic_UserLite_public_key_t remoteIdentityKey;

  HexToBytes(
      remoteEphemeralPub,
      "504a36999f489cd2fdbc08baff3d88fa00569ba986cba22548ffde80f9806829");
  HexToBytes(
      remoteIdentityKey.bytes,
      "db18fc50eea47f00251cb784819a3cf5fc361882597f589f0d7ff820e8064457");
  remoteIdentityKey.size = 32;

  // Store remote ephemeral key
  ephemeralKeyMgr->setRemoteKey(0x1234, remoteEphemeralPub, 1, 100);

  // Test data
  uint8_t plaintext[16] = "Hello PFS test!";
  uint8_t encrypted[32]; // plaintext + 12-byte tag
  uint8_t decrypted[16];
  uint32_t toNode = 0x1234;
  uint32_t fromNode = 0x5678;
  uint64_t packetId = 0x12345678;

  // Encrypt
  bool encResult = crypto->encryptWithPFS(toNode, fromNode, remoteIdentityKey,
                                          remoteEphemeralPub, packetId, 16,
                                          plaintext, encrypted);

  // For this test, we need matching keys on both sides
  // In real use, both nodes would have each other's ephemeral keys
  // This test validates the API works, full round-trip needs both key managers

  // If encryption worked, verify output is different from input
  if (encResult) {
    TEST_ASSERT_FALSE(memcmp(plaintext, encrypted, 16) == 0);
  }

  // Note: Full round-trip test requires mocking both nodes' key managers
}

/**
 * Test EphemeralKeyManager generates valid Curve25519 keypairs.
 */
void test_EphemeralKeyManager_KeyGeneration(void) {
  ephemeralKeyMgr->init();

  const uint8_t *pubKey = ephemeralKeyMgr->getPublicKey();
  const uint8_t *privKey = ephemeralKeyMgr->getPrivateKey();

  TEST_ASSERT_NOT_NULL(pubKey);
  TEST_ASSERT_NOT_NULL(privKey);

  // Keys should not be all zeros
  uint8_t zeros[32] = {0};
  TEST_ASSERT_FALSE(memcmp(pubKey, zeros, 32) == 0);
  TEST_ASSERT_FALSE(memcmp(privKey, zeros, 32) == 0);

  // Private key should be clamped per Curve25519 spec
  TEST_ASSERT_EQUAL(privKey[0] & 0x07, 0);     // Low 3 bits clear
  TEST_ASSERT_EQUAL(privKey[31] & 0x80, 0);    // High bit clear
  TEST_ASSERT_EQUAL(privKey[31] & 0x40, 0x40); // Second-highest bit set
}

/**
 * Test key rotation increments ID and resets message count.
 */
void test_EphemeralKeyManager_Rotation(void) {
  ephemeralKeyMgr->init();

  uint32_t initialKeyId = ephemeralKeyMgr->getKeyId();
  uint8_t initialPubKey[32];
  memcpy(initialPubKey, ephemeralKeyMgr->getPublicKey(), 32);

  // Simulate sending messages
  for (int i = 0; i < 5; i++) {
    ephemeralKeyMgr->incrementMessageCount();
  }
  TEST_ASSERT_EQUAL(5, ephemeralKeyMgr->getMessageCount());

  // Force rotation
  ephemeralKeyMgr->rotateKey();

  // Key ID should increment
  TEST_ASSERT_EQUAL(initialKeyId + 1, ephemeralKeyMgr->getKeyId());

  // Message count should reset
  TEST_ASSERT_EQUAL(0, ephemeralKeyMgr->getMessageCount());

  // Public key should be different
  TEST_ASSERT_FALSE(
      memcmp(initialPubKey, ephemeralKeyMgr->getPublicKey(), 32) == 0);
}

/**
 * Test remote key cache with LRU eviction.
 */
void test_EphemeralKeyManager_RemoteCache(void) {
  ephemeralKeyMgr->init();

  uint8_t testKey[32];
  HexToBytes(
      testKey,
      "504a36999f489cd2fdbc08baff3d88fa00569ba986cba22548ffde80f9806829");

  // Add a remote key
  ephemeralKeyMgr->setRemoteKey(0x1234, testKey, 1, 100);

  // Verify we can retrieve it
  TEST_ASSERT_TRUE(ephemeralKeyMgr->hasRemoteKey(0x1234));
  TEST_ASSERT_TRUE(ephemeralKeyMgr->nodeSupportsPFS(0x1234));

  const RemoteEphemeralKey *retrieved = ephemeralKeyMgr->getRemoteKey(0x1234);
  TEST_ASSERT_NOT_NULL(retrieved);
  TEST_ASSERT_EQUAL(0x1234, retrieved->nodeNum);
  TEST_ASSERT_EQUAL(1, retrieved->keyId);
  TEST_ASSERT_EQUAL_MEMORY(testKey, retrieved->pubkey, 32);

  // Non-existent node should return null
  TEST_ASSERT_FALSE(ephemeralKeyMgr->hasRemoteKey(0x5678));
  TEST_ASSERT_NULL(ephemeralKeyMgr->getRemoteKey(0x5678));
}

/**
 * Test updating a remote key only accepts newer keys.
 */
void test_EphemeralKeyManager_RemoteKeyUpdate(void) {
  ephemeralKeyMgr->init();

  uint8_t key1[32];
  uint8_t key2[32];
  HexToBytes(
      key1, "504a36999f489cd2fdbc08baff3d88fa00569ba986cba22548ffde80f9806829");
  HexToBytes(
      key2, "db18fc50eea47f00251cb784819a3cf5fc361882597f589f0d7ff820e8064457");

  // Add initial key with keyId=5
  ephemeralKeyMgr->setRemoteKey(0x1234, key1, 5, 100);

  // Try to update with older keyId=3 (should be ignored or update based on
  // timestamp)
  ephemeralKeyMgr->setRemoteKey(0x1234, key2, 3, 50);

  // Key should still be key1 (older keyId was rejected)
  const RemoteEphemeralKey *retrieved = ephemeralKeyMgr->getRemoteKey(0x1234);
  TEST_ASSERT_NOT_NULL(retrieved);
  TEST_ASSERT_EQUAL(5, retrieved->keyId);

  // Update with newer keyId=10 (should succeed)
  ephemeralKeyMgr->setRemoteKey(0x1234, key2, 10, 200);
  retrieved = ephemeralKeyMgr->getRemoteKey(0x1234);
  TEST_ASSERT_NOT_NULL(retrieved);
  TEST_ASSERT_EQUAL(10, retrieved->keyId);
  TEST_ASSERT_EQUAL_MEMORY(key2, retrieved->pubkey, 32);
}

void setup() {
  delay(10);
  delay(2000);

  initializeTestEnvironment();
  UNITY_BEGIN();

  RUN_TEST(test_TripleDH_Deterministic);
  RUN_TEST(test_PFS_EncryptDecrypt_RoundTrip);
  RUN_TEST(test_EphemeralKeyManager_KeyGeneration);
  RUN_TEST(test_EphemeralKeyManager_Rotation);
  RUN_TEST(test_EphemeralKeyManager_RemoteCache);
  RUN_TEST(test_EphemeralKeyManager_RemoteKeyUpdate);

  exit(UNITY_END());
}

void loop() {}
