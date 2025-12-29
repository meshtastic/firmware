#include "EphemeralKeyManager.h"
#include "DebugConfiguration.h"
#include "FSCommon.h"
#include "RTC.h"
#include "modules/NodeInfoModule.h"

#if !(MESHTASTIC_EXCLUDE_PKI)
#include <Curve25519.h>
#endif

// File path for persistent storage
#define EPHEMERAL_KEY_FILE "/prefs/ephemeral_keys.dat"
#define EPHEMERAL_KEY_MAGIC 0x45504853 // "EPHS" in little-endian
#define EPHEMERAL_KEY_VERSION 2        // Increment when format changes

// Global instance
EphemeralKeyManager *ephemeralKeyMgr = nullptr;

EphemeralKeyManager::EphemeralKeyManager()
    : localKeyId(0), localKeyTimestamp(0), messagesSinceRotation(0),
      remoteKeyCount(0), initialized(false) {
  memset(localPubKey, 0, EPHEMERAL_KEY_SIZE);
  memset(localPrivKey, 0, EPHEMERAL_KEY_SIZE);
  memset(remoteKeys, 0, sizeof(remoteKeys));
}

EphemeralKeyManager::~EphemeralKeyManager() {
  // Zero out private key material on destruction
  memset(localPrivKey, 0, EPHEMERAL_KEY_SIZE);
}

void EphemeralKeyManager::init() {
#if !(MESHTASTIC_EXCLUDE_PKI)
  // Try to load existing keys from flash
  if (loadFromDisk()) {
    // Calculate key age for detailed logging
    uint32_t now = getValidTime(RTCQualityFromNet);
    uint32_t keyAgeHours = 0;
    if (now > 0 && localKeyTimestamp > 0 && now > localKeyTimestamp) {
      keyAgeHours = (now - localKeyTimestamp) / 3600;
    }

    LOG_INFO("PFS: Loaded from flash - keyId=%u, age=%u hours, msgs=%u/%u, "
             "remoteKeys=%u",
             localKeyId, keyAgeHours, messagesSinceRotation,
             DEFAULT_EPHEMERAL_KEY_ROTATION_MESSAGES, remoteKeyCount);

    // Validate: derive public key from private key and compare
    uint8_t derivedPubKey[EPHEMERAL_KEY_SIZE];
    Curve25519::eval(derivedPubKey, localPrivKey, nullptr);
    if (memcmp(derivedPubKey, localPubKey, EPHEMERAL_KEY_SIZE) != 0) {
      LOG_WARN("PFS: Key validation failed (corrupted?), regenerating");
      generateKeyPair();
      localKeyId++;
      localKeyTimestamp = getValidTime(RTCQualityFromNet);
      if (localKeyTimestamp == 0) {
        localKeyTimestamp = millis() / 1000;
      }
      messagesSinceRotation = 0;
      saveToDisk();
    }

    // Check if rotation is needed
    if (shouldRotate()) {
      LOG_INFO("PFS: Key rotation triggered at boot");
      rotateKey();
    }
  } else {
    // No existing keys, generate fresh ones
    LOG_INFO("PFS: No saved keys found, generating initial key pair");
    generateKeyPair();
    localKeyId = 1;
    localKeyTimestamp = getValidTime(RTCQualityFromNet);
    if (localKeyTimestamp == 0) {
      localKeyTimestamp = millis() / 1000; // Fallback to uptime
    }
    messagesSinceRotation = 0;
    saveToDisk();
  }

  initialized = true;

#ifdef PFS_TEST_MODE
  LOG_WARN("PFS: TEST MODE ENABLED - rotation after %u messages",
           DEFAULT_EPHEMERAL_KEY_ROTATION_MESSAGES);
#endif

  LOG_INFO("PFS: Ready - keyId=%u, pubkey[0:3]=%02x%02x%02x%02x", localKeyId,
           localPubKey[0], localPubKey[1], localPubKey[2], localPubKey[3]);
#else
  LOG_DEBUG("PKI disabled, EphemeralKeyManager not initialized");
#endif
}

void EphemeralKeyManager::generateKeyPair() {
#if !(MESHTASTIC_EXCLUDE_PKI)
  // Generate random private key using hardware RNG
  for (int i = 0; i < EPHEMERAL_KEY_SIZE; i++) {
    localPrivKey[i] = random(256);
  }

  // Clamp the private key per Curve25519 spec
  localPrivKey[0] &= 248;
  localPrivKey[31] &= 127;
  localPrivKey[31] |= 64;

  // Derive public key from private key
  Curve25519::eval(localPubKey, localPrivKey, nullptr);

  LOG_DEBUG("Generated ephemeral key pair");
#endif
}

void EphemeralKeyManager::rotateKey() {
#if !(MESHTASTIC_EXCLUDE_PKI)
  // Generate new key pair
  generateKeyPair();

  // Increment key ID
  localKeyId++;

  // Update timestamp
  localKeyTimestamp = getValidTime(RTCQualityFromNet);
  if (localKeyTimestamp == 0) {
    localKeyTimestamp = millis() / 1000;
  }

  // Reset message counter
  messagesSinceRotation = 0;

  // Persist to flash
  saveToDisk();

  LOG_INFO("PFS: Key rotated, new keyId=%u, broadcasting updated NodeInfo",
           localKeyId);

  // Broadcast new key so peers can update their cache before next message
  if (nodeInfoModule) {
    nodeInfoModule->sendOurNodeInfo();
    LOG_DEBUG("PFS: NodeInfo broadcast triggered after key rotation");
  }
#endif
}

const uint8_t *EphemeralKeyManager::getPublicKey() const {
  return initialized ? localPubKey : nullptr;
}

const uint8_t *EphemeralKeyManager::getPrivateKey() const {
  return initialized ? localPrivKey : nullptr;
}

uint32_t EphemeralKeyManager::getKeyId() const { return localKeyId; }

uint32_t EphemeralKeyManager::getKeyTimestamp() const {
  return localKeyTimestamp;
}

bool EphemeralKeyManager::shouldRotate() const {
  // Check message count threshold
  if (messagesSinceRotation >= DEFAULT_EPHEMERAL_KEY_ROTATION_MESSAGES) {
    LOG_DEBUG("Rotation needed: message limit reached (%d)",
              messagesSinceRotation);
    return true;
  }

  // Check key age threshold
  uint32_t now = getValidTime(RTCQualityFromNet);
  if (now > 0 && localKeyTimestamp > 0) {
    uint32_t ageHours = (now - localKeyTimestamp) / 3600;
    if (ageHours >= DEFAULT_EPHEMERAL_KEY_ROTATION_HOURS) {
      LOG_DEBUG("Rotation needed: key age %u hours", ageHours);
      return true;
    }
  }

  return false;
}

void EphemeralKeyManager::incrementMessageCount() {
  messagesSinceRotation++;

  // Check if rotation is needed after increment
  if (shouldRotate()) {
    rotateKey();
  }
}

void EphemeralKeyManager::setRemoteKey(uint32_t nodeNum, const uint8_t *pubkey,
                                       uint32_t keyId, uint32_t timestamp) {
  // Check if we already have a key for this node
  for (int i = 0; i < remoteKeyCount; i++) {
    if (remoteKeys[i].nodeNum == nodeNum) {
      // Update only if newer key
      if (keyId > remoteKeys[i].keyId || timestamp > remoteKeys[i].timestamp) {
        memcpy(remoteKeys[i].pubkey, pubkey, EPHEMERAL_KEY_SIZE);
        remoteKeys[i].keyId = keyId;
        remoteKeys[i].timestamp = timestamp;
        remoteKeys[i].lastUsed = millis() / 1000;
        LOG_DEBUG("Updated ephemeral key for node %08x, keyId=%u", nodeNum,
                  keyId);
      }
      return;
    }
  }

  // Find a slot for new entry
  int slot = findSlotForRemoteKey();

  remoteKeys[slot].nodeNum = nodeNum;
  memcpy(remoteKeys[slot].pubkey, pubkey, EPHEMERAL_KEY_SIZE);
  remoteKeys[slot].keyId = keyId;
  remoteKeys[slot].timestamp = timestamp;
  remoteKeys[slot].lastUsed = millis() / 1000;

  if (slot >= remoteKeyCount) {
    remoteKeyCount++;
  }

  LOG_DEBUG("Stored ephemeral key for node %08x, keyId=%u", nodeNum, keyId);
}

const RemoteEphemeralKey *
EphemeralKeyManager::getRemoteKey(uint32_t nodeNum) const {
  for (int i = 0; i < remoteKeyCount; i++) {
    if (remoteKeys[i].nodeNum == nodeNum) {
      return &remoteKeys[i];
    }
  }
  return nullptr;
}

bool EphemeralKeyManager::hasRemoteKey(uint32_t nodeNum) const {
  return getRemoteKey(nodeNum) != nullptr;
}

bool EphemeralKeyManager::nodeSuportsPFS(uint32_t nodeNum) const {
  const RemoteEphemeralKey *key = getRemoteKey(nodeNum);
  if (key == nullptr) {
    return false;
  }

  // Check that the key is not all zeros (valid key exists)
  for (int i = 0; i < EPHEMERAL_KEY_SIZE; i++) {
    if (key->pubkey[i] != 0) {
      return true;
    }
  }
  return false;
}

void EphemeralKeyManager::removeRemoteKey(uint32_t nodeNum) {
  for (int i = 0; i < remoteKeyCount; i++) {
    if (remoteKeys[i].nodeNum == nodeNum) {
      // Shift remaining entries
      for (int j = i; j < remoteKeyCount - 1; j++) {
        remoteKeys[j] = remoteKeys[j + 1];
      }
      remoteKeyCount--;
      memset(&remoteKeys[remoteKeyCount], 0, sizeof(RemoteEphemeralKey));
      LOG_DEBUG("Removed ephemeral key for node %08x", nodeNum);
      return;
    }
  }
}

void EphemeralKeyManager::clearRemoteKeys() {
  memset(remoteKeys, 0, sizeof(remoteKeys));
  remoteKeyCount = 0;
  LOG_DEBUG("Cleared all remote ephemeral keys");
}

int EphemeralKeyManager::findSlotForRemoteKey() {
  // If there's room, use next slot
  if (remoteKeyCount < MAX_EPHEMERAL_KEY_CACHE) {
    return remoteKeyCount;
  }

  // Otherwise, find LRU entry
  uint32_t oldestTime = UINT32_MAX;
  int oldestSlot = 0;

  for (int i = 0; i < MAX_EPHEMERAL_KEY_CACHE; i++) {
    if (remoteKeys[i].lastUsed < oldestTime) {
      oldestTime = remoteKeys[i].lastUsed;
      oldestSlot = i;
    }
  }

  LOG_DEBUG("Evicting LRU ephemeral key for node %08x",
            remoteKeys[oldestSlot].nodeNum);
  return oldestSlot;
}

void EphemeralKeyManager::saveToDisk() {
#ifdef FSCom
  auto file = FSCom.open(EPHEMERAL_KEY_FILE, FILE_O_WRITE);
  if (!file) {
    LOG_ERROR("PFS: Failed to open ephemeral key file for writing");
    return;
  }

  // Write magic number and version
  uint32_t magic = EPHEMERAL_KEY_MAGIC;
  uint8_t version = EPHEMERAL_KEY_VERSION;
  file.write((uint8_t *)&magic, sizeof(magic));
  file.write(&version, sizeof(version));

  // Write local key data
  file.write(localPubKey, EPHEMERAL_KEY_SIZE);
  file.write(localPrivKey, EPHEMERAL_KEY_SIZE);
  file.write((uint8_t *)&localKeyId, sizeof(localKeyId));
  file.write((uint8_t *)&localKeyTimestamp, sizeof(localKeyTimestamp));
  file.write((uint8_t *)&messagesSinceRotation, sizeof(messagesSinceRotation));

  // Write remote key count and cache
  file.write((uint8_t *)&remoteKeyCount, sizeof(remoteKeyCount));
  file.write((uint8_t *)remoteKeys, sizeof(remoteKeys));

  // Calculate and write simple checksum (XOR of all key data bytes)
  uint32_t checksum = 0;
  for (int i = 0; i < EPHEMERAL_KEY_SIZE; i++) {
    checksum ^= (localPubKey[i] << (i % 24));
    checksum ^= (localPrivKey[i] << ((i + 8) % 24));
  }
  checksum ^= localKeyId;
  checksum ^= localKeyTimestamp;
  checksum ^= messagesSinceRotation;
  file.write((uint8_t *)&checksum, sizeof(checksum));

  file.close();
  LOG_DEBUG("PFS: Saved state to disk (checksum=%08x)", checksum);
#endif
}

bool EphemeralKeyManager::loadFromDisk() {
#ifdef FSCom
  if (!FSCom.exists(EPHEMERAL_KEY_FILE)) {
    return false;
  }

  auto file = FSCom.open(EPHEMERAL_KEY_FILE, FILE_O_READ);
  if (!file) {
    return false;
  }

  // Verify magic number
  uint32_t magic;
  if (file.read((uint8_t *)&magic, sizeof(magic)) != sizeof(magic) ||
      magic != EPHEMERAL_KEY_MAGIC) {
    file.close();
    LOG_WARN("PFS: Invalid file format (bad magic)");
    return false;
  }

  // Check version
  uint8_t version;
  if (file.read(&version, sizeof(version)) != sizeof(version)) {
    file.close();
    LOG_WARN("PFS: Failed to read version");
    return false;
  }

  // Handle version 1 files (no checksum) vs version 2+ (with checksum)
  bool hasChecksum = (version >= 2);
  if (version > EPHEMERAL_KEY_VERSION) {
    LOG_WARN("PFS: File version %u newer than supported %u, regenerating",
             version, EPHEMERAL_KEY_VERSION);
    file.close();
    return false;
  }

  // Read local key data
  if (file.read(localPubKey, EPHEMERAL_KEY_SIZE) != EPHEMERAL_KEY_SIZE ||
      file.read(localPrivKey, EPHEMERAL_KEY_SIZE) != EPHEMERAL_KEY_SIZE ||
      file.read((uint8_t *)&localKeyId, sizeof(localKeyId)) !=
          sizeof(localKeyId) ||
      file.read((uint8_t *)&localKeyTimestamp, sizeof(localKeyTimestamp)) !=
          sizeof(localKeyTimestamp) ||
      file.read((uint8_t *)&messagesSinceRotation,
                sizeof(messagesSinceRotation)) !=
          sizeof(messagesSinceRotation)) {
    file.close();
    LOG_WARN("PFS: Failed to read key data (corrupted?)");
    return false;
  }

  // Read remote key cache
  if (file.read((uint8_t *)&remoteKeyCount, sizeof(remoteKeyCount)) ==
      sizeof(remoteKeyCount)) {
    file.read((uint8_t *)remoteKeys, sizeof(remoteKeys));
  }

  // Verify checksum if present
  if (hasChecksum) {
    uint32_t storedChecksum;
    if (file.read((uint8_t *)&storedChecksum, sizeof(storedChecksum)) ==
        sizeof(storedChecksum)) {
      // Calculate expected checksum
      uint32_t expectedChecksum = 0;
      for (int i = 0; i < EPHEMERAL_KEY_SIZE; i++) {
        expectedChecksum ^= (localPubKey[i] << (i % 24));
        expectedChecksum ^= (localPrivKey[i] << ((i + 8) % 24));
      }
      expectedChecksum ^= localKeyId;
      expectedChecksum ^= localKeyTimestamp;
      expectedChecksum ^= messagesSinceRotation;

      if (storedChecksum != expectedChecksum) {
        LOG_WARN(
            "PFS: Checksum mismatch (stored=%08x, expected=%08x), regenerating",
            storedChecksum, expectedChecksum);
        file.close();
        return false;
      }
      LOG_DEBUG("PFS: Checksum verified OK");
    }
  }

  file.close();
  return true;
#else
  return false;
#endif
}
