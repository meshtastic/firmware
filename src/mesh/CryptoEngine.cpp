#include "configuration.h"
#include "CryptoEngine.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include <Crypto.h>
#include <Curve25519.h>
#include <BLAKE2b.h>

/**
 * Create a public/private key pair with Curve25519.
 * 
 * @param pubKey The destination for the public key.
 * @param privKey The destination for the private key.
 */
void CryptoEngine::generateKeyPair(uint8_t *pubKey, uint8_t *privKey)
{
    DEBUG_MSG("Generating Curve25519 key pair...\n");
    Curve25519::dh1(public_key, private_key);
    memcpy(pubKey, public_key, sizeof(public_key));
    memcpy(privKey, private_key, sizeof(private_key));
}

/**
 * Encrypt a packet's payload using a key generated with Curve25519 and Blake2b
 * for a specific node.
 *
 * @param bytes is updated in place
 */
void CryptoEngine::encryptCurve25519_Blake2b(uint32_t toNode, uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
{
    NodeInfo node = *nodeDB.getNode(toNode);
    if (node.num < 1 || node.user.public_key[0] == 0) {
        DEBUG_MSG("Node %d or their public_key not found\n", toNode);
        return;
    }
    
    // Calculate the shared secret with the destination node and encrypt
    crypto->setDHKey(toNode);
    crypto->encrypt(fromNode, packetNum, numBytes, bytes);
}

/**
 * Decrypt a packet's payload using a key generated with Curve25519 and Blake2b
 * for a specific node.
 *
 * @param bytes is updated in place
 */
void CryptoEngine::decryptCurve25519_Blake2b(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
{
    NodeInfo node = *nodeDB.getNode(fromNode);
    if (node.num < 1 || node.user.public_key[0] == 0)
    {
        DEBUG_MSG("Node or its public key not found in database\n");
        return;
    }

    // Calculate the shared secret with the sending node and decrypt
    crypto->setDHKey(fromNode);
    crypto->decrypt(fromNode, packetNum, numBytes, bytes);
}

/**
 * Set the key used for encrypt, decrypt.
 *
 * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
 *
 * @param nodeNum the node number of the node who's public key we want to use
 */
void CryptoEngine::setDHKey(uint32_t nodeNum)
{
    NodeInfo node = *nodeDB.getNode(nodeNum);
    if (node.num < 1 || node.user.public_key[0] == 0) {
        DEBUG_MSG("Node %d or their public_key not found\n", nodeNum);
        return;
    }
    
    // Calculate the shared secret with the specified node's
    // public key and our private key
    uint8_t *pubKey = node.user.public_key;
    uint8_t shared_key[32];
    uint8_t local_priv[32];
    memcpy(shared_key, pubKey, 32);
    memcpy(local_priv, private_key, 32);
    Curve25519::dh2(shared_key, local_priv);

    /**
     * D.J. Bernstein reccomends hashing the shared key. We want to do this because there are
     * at least 128 bits of entropy in the 256-bit output of the DH key exchange, but we don't
     * really know where. If you extract, for instance, the first 128 bits with basic truncation,
     * then you don't know if you got all of your 128 entropy bits, or less, possibly much less.
     * 
     * No exploitable bias is really known at that point, but we know enough to be wary.
     * Hashing the DH output is a simple and safe way to gather all the entropy and spread
     * it around as needed.
     */
    crypto->hash(shared_key, 32);

    CryptoKey k;
    memcpy(k.bytes, shared_key, 32);
    k.length = 32;
    crypto->setKey(k);
}

/**
 * Hash arbitrary data using BLAKE2b.
 * 
 * @param bytes 
 * @param numBytes 
 */
void CryptoEngine::hash(uint8_t *bytes, size_t numBytes)
{
    BLAKE2b hash;
    size_t posn, len;
    uint8_t size = numBytes;
    uint8_t inc = 16;
    hash.reset();
    for (posn = 0; posn < size; posn += inc) {
        len = size - posn;
        if (len > inc)
            len = inc;
        hash.update(bytes + posn, len);
    }
    hash.finalize(bytes, 32);
}

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