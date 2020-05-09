#pragma once

#include <Arduino.h>

/**
 * see docs/software/crypto.md for details.
 *
 * The NONCE is constructed by concatenating:
 * a 32 bit sending node number + a 64 bit packet number + a 32 bit block counter (starts at zero)
 */

class CryptoEngine
{
  public:
    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 32 for now (AES256)
     */
    void setKey(size_t numBytes, const uint8_t *bytes);

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    void encrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes);
    void decrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes);
};