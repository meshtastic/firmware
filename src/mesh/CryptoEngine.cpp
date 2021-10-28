#include "configuration.h"
#include "CryptoEngine.h"

void CryptoEngine::setKey(const CryptoKey &k)
{
    DEBUG_MSG("Installing AES%d key!\n", k.length * 8);
    /* for(uint8_t i = 0; i < k.length; i++)
        DEBUG_MSG("%02x ", k.bytes[i]);
    DEBUG_MSG("\n"); */

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

    // use memcpy to avoid breaking strict-aliasing
    memcpy(nonce, &packetNum, sizeof(uint64_t));
    memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t));
    //*((uint64_t *)&nonce[0]) = packetNum;
    //*((uint32_t *)&nonce[8]) = fromNode;
}