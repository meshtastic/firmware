#pragma once

#include "concurrency/LockGuard.h"
#include <Arduino.h>

extern concurrency::Lock *cryptLock;

struct CryptoKey {
    uint8_t bytes[32];

    /// # of bytes, or -1 to mean "invalid key - do not use"
    int8_t length;
};

/**
 * see docs/software/crypto.md for details.
 *
 */

#define MAX_BLOCKSIZE 256

class CryptoEngine
{
  protected:
    /** Our per packet nonce */
    uint8_t nonce[16] = {0};

    CryptoKey key = {};

  public:
    virtual ~CryptoEngine() {}

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a _static_ buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(const CryptoKey &k);

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);
    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);

  protected:
    /**
     * Init our 128 bit nonce for a new packet
     *
     * The NONCE is constructed by concatenating (from MSB to LSB):
     * a 64 bit packet number (stored in little endian order)
     * a 32 bit sending node number (stored in little endian order)
     * a 32 bit block counter (starts at zero)
     */
    void initNonce(uint32_t fromNode, uint64_t packetId);
};

extern CryptoEngine *crypto;
