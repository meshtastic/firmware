#include "CryptoEngine.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "configuration.h"

#if !(MESHTASTIC_EXCLUDE_PKI)
#include "aes-ccm.h"
#include <Crypto.h>
#include <Curve25519.h>
#include <SHA256.h>
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN)
/**
 * Create a public/private key pair with Curve25519.
 *
 * @param pubKey The destination for the public key.
 * @param privKey The destination for the private key.
 */
void CryptoEngine::generateKeyPair(uint8_t *pubKey, uint8_t *privKey)
{
    LOG_DEBUG("Generating Curve25519 key pair...\n");
    Curve25519::dh1(public_key, private_key);
    memcpy(pubKey, public_key, sizeof(public_key));
    memcpy(privKey, private_key, sizeof(private_key));
}
#endif
uint8_t shared_key[32];
void CryptoEngine::clearKeys()
{
    memset(public_key, 0, sizeof(public_key));
    memset(private_key, 0, sizeof(private_key));
}

/**
 * Encrypt a packet's payload using a key generated with Curve25519 and Blake2b
 * for a specific node.
 *
 * @param bytes is updated in place
 */
void CryptoEngine::encryptCurve25519(uint32_t toNode, uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes,
                                     uint8_t *bytesOut)
{
    uint8_t *auth;
    auth = bytesOut + numBytes;
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(toNode);
    if (node->num < 1 || node->user.public_key.size == 0) {
        LOG_DEBUG("Node %d or their public_key not found\n", toNode);
        return;
    }
    crypto->setDHKey(toNode);
    initNonce(fromNode, packetNum);

    // Calculate the shared secret with the destination node and encrypt
    LOG_DEBUG("Attempting encrypt using nonce: 0x%16x shared_key: 0x%32x\n", nonce, shared_key);
    aes_ccm_ae(shared_key, 32, nonce, 8, bytes, numBytes, nullptr, 0, bytesOut, auth);
}

/**
 * Decrypt a packet's payload using a key generated with Curve25519 and SHA256
 * for a specific node.
 *
 * @param bytes is updated in place
 */
bool CryptoEngine::decryptCurve25519(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes, uint8_t *bytesOut)
{
    uint8_t *auth; // set to last 8 bytes of text?
    auth = bytes + numBytes - 8;
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(fromNode);

    if (node == nullptr || node->num < 1 || node->user.public_key.size == 0) {
        LOG_DEBUG("Node or its public key not found in database\n");
        return false;
    }

    // Calculate the shared secret with the sending node and decrypt
    crypto->setDHKey(fromNode);
    LOG_DEBUG("Decrypting using PKI!\n");
    initNonce(fromNode, packetNum);
    LOG_DEBUG("Attempting decrypt using nonce: 0x%16x shared_key: 0x%32x\n", nonce, shared_key);
    return aes_ccm_ad(shared_key, 32, nonce, 8, bytes, numBytes, nullptr, 0, auth, bytesOut);
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
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);
    if (node->num < 1 || node->user.public_key.bytes[0] == 0) {
        LOG_DEBUG("Node %d or their public_key not found\n", nodeNum);
        return;
    }

    // Calculate the shared secret with the specified node's
    // public key and our private key
    uint8_t *pubKey = node->user.public_key.bytes;

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
}

/**
 * Hash arbitrary data using SHA256.
 *
 * @param bytes
 * @param numBytes
 */
void CryptoEngine::hash(uint8_t *bytes, size_t numBytes)
{
    SHA256 hash;
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

void CryptoEngine::aesSetKey(const uint8_t *key_bytes, size_t key_len)
{
    if (aes) {
        delete aes;
        aes = nullptr;
    }
    if (key.length != 0) {
        aes = new AESSmall256();
        aes->setKey(key_bytes, key_len);
    }
}

void CryptoEngine::aesEncrypt(uint8_t *in, uint8_t *out)
{
    aes->encryptBlock(out, in);
}

#endif

concurrency::Lock *cryptLock;

void CryptoEngine::setKey(const CryptoKey &k)
{
    LOG_DEBUG("Using AES%d key!\n", k.length * 8);
    key = k;
}

/**
 * Encrypt a packet
 *
 * @param bytes is updated in place
 */
void CryptoEngine::encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes)
{
    LOG_WARN("noop encryption!\n");
}

void CryptoEngine::decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes)
{
    LOG_WARN("noop decryption!\n");
}

/**
 * Init our 128 bit nonce for a new packet
 */
void CryptoEngine::initNonce(uint32_t fromNode, uint64_t packetId)
{
    memset(nonce, 0, sizeof(nonce));

    // use memcpy to avoid breaking strict-aliasing
    memcpy(nonce, &packetId, sizeof(uint64_t));
    memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t));
}