#pragma once
#include "AES.h"
#include "CTR.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include <Arduino.h>
#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
extern "C" {
    #include "kem.h"
}
#endif

extern concurrency::Lock *cryptLock;

struct CryptoKey {
    uint8_t bytes[32];

    /// # of bytes, or -1 to mean "invalid key - do not use"
    int8_t length;
};

// Kyber constants 
#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
#define KYBER_PUBLICKEYBYTES  800   // Kyber-512 public key size
#define KYBER_SECRETKEYBYTES  1632  // Kyber-512 private key size  
#define KYBER_CIPHERTEXTBYTES 768   // Kyber-512 ciphertext size
#define KYBER_SSBYTES         32    // Shared secret size
#define KYBER_SYMBYTES        32    // Seed/randomness size
#endif

/**
 * see docs/software/crypto.md for details.
 *
 */
#define MAX_BLOCKSIZE 256
#define TEST_CURVE25519_FIELD_OPS // Exposes Curve25519::isWeakPoint() for testing keys

class CryptoEngine
{
public:
#if !(MESHTASTIC_EXCLUDE_PKI)
    uint8_t public_key[32] = {0};
#endif

    virtual ~CryptoEngine() {}

#if !(MESHTASTIC_EXCLUDE_PKI)
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN)
    virtual void generateKeyPair(uint8_t *pubKey, uint8_t *privKey);
    virtual bool regeneratePublicKey(uint8_t *pubKey, uint8_t *privKey);
    
#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
    // Post-quantum key generation and management
    virtual bool generateKyberKeyPair(uint8_t *pubKey, uint8_t *privKey);
    virtual bool loadKyberKeys(const uint8_t *pubKey, const uint8_t *privKey);
    virtual bool getKyberPublicKey(uint8_t *pubKey);
    virtual void clearKyberKeys();
    virtual bool hasValidKyberKeys() const;
    
    // Post-quantum encryption/decryption
    virtual bool encryptKyber(uint32_t toNode, uint32_t fromNode, const uint8_t *remotePubKey,
                             uint64_t packetNum, size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut);
    virtual bool decryptKyber(uint32_t fromNode, uint64_t packetNum, size_t numBytes, 
                             const uint8_t *bytes, uint8_t *bytesOut);
    
    // Utility functions
    virtual void deriveSessionKey(const uint8_t *shared_secret, size_t secret_len, 
                                 uint32_t fromNode, uint32_t toNode, uint8_t *session_key);
    virtual size_t getKyberPacketOverhead();
#endif
#endif

    void clearKeys();
    void setDHPrivateKey(uint8_t *_private_key);
    virtual bool encryptCurve25519(uint32_t toNode, uint32_t fromNode, meshtastic_UserLite_public_key_t remotePublic,
                                  uint64_t packetNum, size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut);
    virtual bool decryptCurve25519(uint32_t fromNode, meshtastic_UserLite_public_key_t remotePublic, uint64_t packetNum,
                                  size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut);
    virtual bool setDHPublicKey(uint8_t *publicKey);
    virtual void hash(uint8_t *bytes, size_t numBytes);
    virtual void aesSetKey(const uint8_t *key, size_t key_len);
    virtual void aesEncrypt(uint8_t *in, uint8_t *out);

    AESSmall256 *aes = NULL;
#endif

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume no encryption and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a static buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(const CryptoKey &k);

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encryptPacket(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);
    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);
    virtual void encryptAESCtr(CryptoKey key, uint8_t *nonce, size_t numBytes, uint8_t *bytes);

#ifndef PIO_UNIT_TESTING
protected:
#endif
    /** Our per packet nonce */
    uint8_t nonce[16] = {0};
    CryptoKey key = {};
    CTRCommon *ctr = NULL;

#if !(MESHTASTIC_EXCLUDE_PKI)
    uint8_t shared_key[32] = {0};
    uint8_t private_key[32] = {0};
    
#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
    // Post-quantum Kyber keys
    uint8_t pq_public_key[CRYPTO_PUBLICKEYBYTES] = {0};
    uint8_t pq_private_key[CRYPTO_SECRETKEYBYTES] = {0};
    bool pq_keys_valid = false;
#endif
#endif

    /**
     * Init our 128 bit nonce for a new packet
     *
     * The NONCE is constructed by concatenating (from MSB to LSB):
     * a 64 bit packet number (stored in little endian order)
     * a 32 bit sending node number (stored in little endian order)
     * a 32 bit block counter (starts at zero)
     */
    void initNonce(uint32_t fromNode, uint64_t packetId, uint32_t extraNonce = 0);
};

extern CryptoEngine *crypto;