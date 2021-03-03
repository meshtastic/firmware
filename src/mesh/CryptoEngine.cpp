#include "CryptoEngine.h"
#include "configuration.h"

void CryptoEngine::setKey(const CryptoKey &k)
{
    DEBUG_MSG("Installing AES%d key!\n", k.length * 8);
    key = k;
}

/**
 * Encrypt a packet
 *
 * @param bytes is updated in place
 */
void CryptoEngine::encrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
{
    DEBUG_MSG("WARNING: noop encryption!\n");
}

void CryptoEngine::decrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
{
    DEBUG_MSG("WARNING: noop decryption!\n");
}

/**
 * Init our 128 bit nonce for a new packet
 */
void CryptoEngine::initNonce(uint32_t fromNode, uint64_t packetNum)
{
    memset(nonce, 0, sizeof(nonce));
    *((uint64_t *)&nonce[0]) = packetNum;
    *((uint32_t *)&nonce[8]) = fromNode;
}