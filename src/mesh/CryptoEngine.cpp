#include "CryptoEngine.h"
#include "configuration.h"

void CryptoEngine::setKey(size_t numBytes, const uint8_t *bytes)
{
    DEBUG_MSG("WARNING: Using stub crypto - all crypto is sent in plaintext!\n");
}

/**
 * Encrypt a packet
 *
 * @param bytes is updated in place
 */
void CryptoEngine::encrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes) {}

void CryptoEngine::decrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes) {}