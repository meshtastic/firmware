#include "CryptoEngine.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "architecture.h"
#include "configuration.h"

#if !(MESHTASTIC_EXCLUDE_PKI)
#include "aes-ccm.h"
#include "meshUtils.h"
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

/**
 * regenerate a public key with Curve25519.
 *
 * @param pubKey The destination for the public key.
 * @param privKey The source for the private key.
 */
bool CryptoEngine::regeneratePublicKey(uint8_t *pubKey, uint8_t *privKey)
{
    if (!memfll(privKey, 0, sizeof(private_key))) {
        Curve25519::eval(pubKey, privKey, 0);
        if (Curve25519::isWeakPoint(pubKey)) {
            LOG_ERROR("PKI key generation failed. Specified private key results in a weak\n");
            memset(pubKey, 0, 32);
            return false;
        }
        memcpy(private_key, privKey, sizeof(private_key));
        memcpy(public_key, pubKey, sizeof(public_key));
    } else {
        LOG_WARN("X25519 key generation failed due to blank private key\n");
        return false;
    }
    return true;
}
#endif
void CryptoEngine::clearKeys()
{
    memset(public_key, 0, sizeof(public_key));
    memset(private_key, 0, sizeof(private_key));
}

/**
 * Encrypt a packet's payload using a key generated with Curve25519 and SHA256
 * for a specific node.
 *
 * @param bytes is updated in place
 */
bool CryptoEngine::encryptCurve25519(uint32_t toNode, uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes,
                                     uint8_t *bytesOut)
{
    uint8_t *auth;
    uint32_t *extraNonce;
    long extraNonceTmp = random();
    auth = bytesOut + numBytes;
    extraNonce = (uint32_t *)(auth + 8);
    memcpy(extraNonce, &extraNonceTmp,
           4); // do not use dereference on potential non aligned pointers : *extraNonce = extraNonceTmp;
    LOG_INFO("Random nonce value: %d\n", extraNonceTmp);
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(toNode);
    if (node->num < 1 || node->user.public_key.size == 0) {
        LOG_DEBUG("Node %d or their public_key not found\n", toNode);
        return false;
    }
    if (!crypto->setDHKey(toNode)) {
        return false;
    }
    initNonce(fromNode, packetNum, extraNonceTmp);

    // Calculate the shared secret with the destination node and encrypt
    printBytes("Attempting encrypt using nonce: ", nonce, 13);
    printBytes("Attempting encrypt using shared_key starting with: ", shared_key, 8);
    aes_ccm_ae(shared_key, 32, nonce, 8, bytes, numBytes, nullptr, 0, bytesOut,
               auth); // this can write up to 15 bytes longer than numbytes past bytesOut
    memcpy(extraNonce, &extraNonceTmp,
           4); // do not use dereference on potential non aligned pointers : *extraNonce = extraNonceTmp;
    return true;
}

/**
 * Decrypt a packet's payload using a key generated with Curve25519 and SHA256
 * for a specific node.
 *
 * @param bytes is updated in place
 */
bool CryptoEngine::decryptCurve25519(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes, uint8_t *bytesOut)
{
    uint8_t *auth;       // set to last 8 bytes of text?
    uint32_t extraNonce; // pointer was not really used
    auth = bytes + numBytes - 12;
#ifndef PIO_UNIT_TESTING
    memcpy(&extraNonce, auth + 8, 4); // do not use dereference on potential non aligned pointers : (uint32_t *)(auth + 8);
    LOG_INFO("Random nonce value: %d\n", extraNonce);
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(fromNode);

    if (node == nullptr || node->num < 1 || node->user.public_key.size == 0) {
        LOG_DEBUG("Node or its public key not found in database\n");
        return false;
    }

    // Calculate the shared secret with the sending node and decrypt
    if (!crypto->setDHKey(fromNode)) {
        return false;
    }
#endif
    initNonce(fromNode, packetNum, extraNonce);
    printBytes("Attempting decrypt using nonce: ", nonce, 13);
    printBytes("Attempting decrypt using shared_key starting with: ", shared_key, 8);
    return aes_ccm_ad(shared_key, 32, nonce, 8, bytes, numBytes - 12, nullptr, 0, auth, bytesOut);
}

void CryptoEngine::setDHPrivateKey(uint8_t *_private_key)
{
    memcpy(private_key, _private_key, 32);
}
/**
 * Set the PKI key used for encrypt, decrypt.
 *
 * @param nodeNum the node number of the node who's public key we want to use
 */
bool CryptoEngine::setDHKey(uint32_t nodeNum)
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);
    if (node->num < 1 || node->user.public_key.size == 0) {
        LOG_DEBUG("Node %d or their public_key not found\n", nodeNum);
        return false;
    }
    printBytes("Generating DH with remote pubkey: ", node->user.public_key.bytes, 32);
    printBytes("And local pubkey: ", config.security.public_key.bytes, 32);
    if (!setDHPublicKey(node->user.public_key.bytes))
        return false;

    // printBytes("DH Output: ", shared_key, 32);

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
    return true;
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
    size_t posn;
    uint8_t size = numBytes;
    uint8_t inc = 16;
    hash.reset();
    for (posn = 0; posn < size; posn += inc) {
        size_t len = size - posn;
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
    if (key_len != 0) {
        aes = new AESSmall256();
        aes->setKey(key_bytes, key_len);
    }
}

void CryptoEngine::aesEncrypt(uint8_t *in, uint8_t *out)
{
    aes->encryptBlock(out, in);
}

bool CryptoEngine::setDHPublicKey(uint8_t *pubKey)
{
    uint8_t local_priv[32];
    memcpy(shared_key, pubKey, 32);
    memcpy(local_priv, private_key, 32);
    // Calculate the shared secret with the specified node's public key and our private key
    // This includes an internal weak key check, which among other things looks for an all 0 public key and shared key.
    if (!Curve25519::dh2(shared_key, local_priv)) {
        LOG_WARN("Curve25519DH step 2 failed!\n");
        return false;
    }
    return true;
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
void CryptoEngine::encryptPacket(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes)
{
    if (key.length > 0) {
        initNonce(fromNode, packetId);
        if (numBytes <= MAX_BLOCKSIZE) {
            encryptAESCtr(key, nonce, numBytes, bytes);
        } else {
            LOG_ERROR("Packet too large for crypto engine: %d. noop encryption!\n", numBytes);
        }
    }
}

void CryptoEngine::decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes)
{
    // For CTR, the implementation is the same
    encryptPacket(fromNode, packetId, numBytes, bytes);
}

// Generic implementation of AES-CTR encryption.
void CryptoEngine::encryptAESCtr(CryptoKey _key, uint8_t *_nonce, size_t numBytes, uint8_t *bytes)
{
    if (ctr) {
        delete ctr;
        ctr = nullptr;
    }
    if (_key.length == 16)
        ctr = new CTR<AES128>();
    else
        ctr = new CTR<AES256>();
    ctr->setKey(_key.bytes, _key.length);
    static uint8_t scratch[MAX_BLOCKSIZE];
    memcpy(scratch, bytes, numBytes);
    memset(scratch + numBytes, 0,
           sizeof(scratch) - numBytes); // Fill rest of buffer with zero (in case cypher looks at it)

    ctr->setIV(_nonce, 16);
    ctr->setCounterSize(4);
    ctr->encrypt(bytes, scratch, numBytes);
}

/**
 * Init our 128 bit nonce for a new packet
 */
void CryptoEngine::initNonce(uint32_t fromNode, uint64_t packetId, uint32_t extraNonce)
{
    memset(nonce, 0, sizeof(nonce));

    // use memcpy to avoid breaking strict-aliasing
    memcpy(nonce, &packetId, sizeof(uint64_t));
    memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t));
    if (extraNonce)
        memcpy(nonce + sizeof(uint32_t), &extraNonce, sizeof(uint32_t));
}
#ifndef HAS_CUSTOM_CRYPTO_ENGINE
CryptoEngine *crypto = new CryptoEngine;
#endif