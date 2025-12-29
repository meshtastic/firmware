#pragma once

#include "configuration.h"
#include <Arduino.h>
#include <stdint.h>

/**
 * Maximum number of remote ephemeral keys to cache.
 * LRU eviction when full.
 */
#define MAX_EPHEMERAL_KEY_CACHE 32

/**
 * Default key rotation interval in hours.
 */
#define DEFAULT_EPHEMERAL_KEY_ROTATION_HOURS 24

/**
 * Default key rotation after N messages.
 * Set PFS_TEST_MODE to reduce this for testing key rotation.
 */
#ifdef PFS_TEST_MODE
#define DEFAULT_EPHEMERAL_KEY_ROTATION_MESSAGES                                \
  5 // Rotate after 5 messages for testing
#else
#define DEFAULT_EPHEMERAL_KEY_ROTATION_MESSAGES 100
#endif

/**
 * Ephemeral key size (Curve25519 = 32 bytes).
 */
#define EPHEMERAL_KEY_SIZE 32

/**
 * Record for a remote node's ephemeral key.
 */
struct RemoteEphemeralKey {
  uint32_t nodeNum;
  uint8_t pubkey[EPHEMERAL_KEY_SIZE];
  uint32_t keyId;
  uint32_t timestamp;
  uint32_t lastUsed; // For LRU eviction
};

/**
 * Manages ephemeral key pairs for Perfect Forward Secrecy.
 *
 * This class handles:
 * - Generation and rotation of local ephemeral key pairs
 * - Storage and retrieval of remote nodes' ephemeral keys
 * - Persistence of keys to flash storage
 * - Key rotation policy enforcement
 *
 * PFS is OPTIONAL - nodes without ephemeral keys fall back to legacy PKI.
 */
class EphemeralKeyManager {
public:
  EphemeralKeyManager();
  ~EphemeralKeyManager();

  /**
   * Initialize the key manager.
   * Loads existing keys from flash or generates new ones.
   */
  void init();

  /**
   * Generate a new ephemeral key pair.
   * Increments key ID and updates timestamp.
   * Persists to flash.
   */
  void rotateKey();

  /**
   * Get the current ephemeral public key.
   * @return Pointer to 32-byte public key, or nullptr if not initialized.
   */
  const uint8_t *getPublicKey() const;

  /**
   * Get the current ephemeral private key.
   * @return Pointer to 32-byte private key, or nullptr if not initialized.
   */
  const uint8_t *getPrivateKey() const;

  /**
   * Get the current key ID.
   * @return Current key rotation counter.
   */
  uint32_t getKeyId() const;

  /**
   * Get the timestamp when current key was generated.
   * @return Seconds since epoch.
   */
  uint32_t getKeyTimestamp() const;

  /**
   * Check if key rotation is needed based on age or message count.
   * @return true if rotation should be performed.
   */
  bool shouldRotate() const;

  /**
   * Increment message counter used for rotation policy.
   */
  void incrementMessageCount();

  /**
   * Store a remote node's ephemeral key.
   * @param nodeNum The remote node number.
   * @param pubkey The 32-byte ephemeral public key.
   * @param keyId The key rotation ID.
   * @param timestamp When the key was generated.
   */
  void setRemoteKey(uint32_t nodeNum, const uint8_t *pubkey, uint32_t keyId,
                    uint32_t timestamp);

  /**
   * Get a remote node's ephemeral key.
   * @param nodeNum The remote node number.
   * @return Pointer to RemoteEphemeralKey, or nullptr if not found.
   */
  const RemoteEphemeralKey *getRemoteKey(uint32_t nodeNum) const;

  /**
   * Check if we have an ephemeral key for a remote node.
   * Used to determine if PFS can be used for this node.
   * @param nodeNum The remote node number.
   * @return true if key exists and is valid.
   */
  bool hasRemoteKey(uint32_t nodeNum) const;

  /**
   * Check if a specific node supports PFS.
   * @param nodeNum The remote node number.
   * @return true if node has broadcast an ephemeral key.
   */
  bool nodeSuportsPFS(uint32_t nodeNum) const;

  /**
   * Remove a remote node's ephemeral key from cache.
   * @param nodeNum The remote node number.
   */
  void removeRemoteKey(uint32_t nodeNum);

  /**
   * Clear all cached remote keys.
   */
  void clearRemoteKeys();

  /**
   * Persist current state to flash.
   */
  void saveToDisk();

  /**
   * Load state from flash.
   * @return true if loaded successfully.
   */
  bool loadFromDisk();

  /**
   * Check if the manager is initialized with valid keys.
   */
  bool isInitialized() const { return initialized; }

  /**
   * Get current message count since last rotation.
   * Used for status reporting and debugging.
   */
  uint32_t getMessageCount() const { return messagesSinceRotation; }

  /**
   * Get number of remote ephemeral keys in cache.
   */
  uint8_t getRemoteKeyCount() const { return remoteKeyCount; }

  /**
   * Get rotation threshold for messages.
   */
  uint32_t getRotationThreshold() const {
    return DEFAULT_EPHEMERAL_KEY_ROTATION_MESSAGES;
  }

private:
  // Local ephemeral key pair
  uint8_t localPubKey[EPHEMERAL_KEY_SIZE];
  uint8_t localPrivKey[EPHEMERAL_KEY_SIZE];
  uint32_t localKeyId;
  uint32_t localKeyTimestamp;
  uint32_t messagesSinceRotation;

  // Remote key cache
  RemoteEphemeralKey remoteKeys[MAX_EPHEMERAL_KEY_CACHE];
  uint8_t remoteKeyCount;

  bool initialized;

  /**
   * Find an empty slot or LRU slot for a new remote key.
   * @return Index into remoteKeys array.
   */
  int findSlotForRemoteKey();

  /**
   * Generate a Curve25519 key pair using hardware RNG.
   */
  void generateKeyPair();
};

// Global instance pointer
extern EphemeralKeyManager *ephemeralKeyMgr;
