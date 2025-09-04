#include "CryptoEngine.h"
// #include "NodeDB.h"
#include "architecture.h"

#if !(MESHTASTIC_EXCLUDE_PKI)
#include "NodeDB.h"
#include "aes-ccm.h"
#include "meshUtils.h"
#include <Crypto.h>
#include <Curve25519.h>
#include <RNG.h>
#include <SHA256.h>

#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
#include <KyberPQ.h>
#endif

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN)
#if !defined(ARCH_STM32WL)
#define CryptRNG RNG
#endif

/**
 * Create a public/private key pair with Curve25519.
 *
 * @param pubKey The destination for the public key.
 * @param privKey The destination for the private key.
 */
void CryptoEngine::generateKeyPair(uint8_t *pubKey, uint8_t *privKey)
{
    // Mix in any randomness we can, to make key generation stronger.
    CryptRNG.begin(optstr(APP_VERSION));
    if (myNodeInfo.device_id.size == 16) {
        CryptRNG.stir(myNodeInfo.device_id.bytes, myNodeInfo.device_id.size);
    }
    auto noise = random();
    CryptRNG.stir((uint8_t *)&noise, sizeof(noise));

    LOG_DEBUG("Generate Curve25519 keypair");
    Curve25519::dh1(public_key, private_key);
    memcpy(pubKey, public_key, sizeof(public_key));
    memcpy(privKey, private_key, sizeof(private_key));
}

#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
/**
 * Generate a Kyber512 keypair for post-quantum encryption
 * 
 * @param pubKey Destination for public key (KYBER_PUBLICKEYBYTES)
 * @param privKey Destination for private key (KYBER_SECRETKEYBYTES)  
 */
bool CryptoEngine::generateKyberKeyPair(uint8_t *pubKey, uint8_t *privKey)
{
    LOG_DEBUG("Generate Kyber keypair via PQCrypto wrapper");

    if (!kyber.generateKeyPair(pubKey, privKey)) {
        LOG_ERROR("Kyber keypair generation failed");
        pq_keys_valid = false;
        return false;
    }

    memcpy(pq_public_key, pubKey, kyber.PublicKeySize);
    memcpy(pq_private_key, privKey, kyber.PrivateKeySize);
    pq_keys_valid = true;

    LOG_INFO("Kyber keypair generated successfully");
    return true;
}



/**
 * Load existing Kyber keys from storage
 */
bool CryptoEngine::loadKyberKeys(const uint8_t *pubKey, const uint8_t *privKey)
{
    if (!pubKey || !privKey) {
        LOG_ERROR("Invalid Kyber keys provided");
        return false;
    }
    
    memcpy(pq_public_key, pubKey, KYBER_PUBLICKEYBYTES);
    memcpy(pq_private_key, privKey, KYBER_SECRETKEYBYTES);
    pq_keys_valid = true;
    
    LOG_INFO("Kyber keys loaded from storage");
    return true;
}

/**
 * Get current Kyber public key
 */
bool CryptoEngine::getKyberPublicKey(uint8_t *pubKey)
{
    if (!pq_keys_valid) {
        LOG_ERROR("No valid Kyber keys available");
        return false;
    }
    
    memcpy(pubKey, pq_public_key, KYBER_PUBLICKEYBYTES);
    return true;
}

/**
 * Encrypt packet using Kyber KEM + AES
 * This is called by the initiator (first sender)
 */
bool CryptoEngine::encryptKyber(uint32_t toNode, uint32_t fromNode, const uint8_t *remotePubKey,
                               uint64_t packetNum, size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut)
{
    if (!pq_keys_valid) {
        LOG_ERROR("No valid Kyber keys for encryption");
        return false;
    }
    if (!remotePubKey) {
        LOG_ERROR("No remote Kyber public key provided");
        return false;
    }

    uint8_t shared_secret[CRYPTO_BYTES];
    uint8_t ciphertext[CRYPTO_CIPHERTEXTBYTES];


    if (!kyber.encap(ciphertext, shared_secret, remotePubKey)) {
        LOG_ERROR("Kyber encapsulation failed with result");
        return false;
    }

    // Derive AES session key
    uint8_t session_key[32];
    deriveSessionKey(shared_secret, CRYPTO_BYTES, fromNode, toNode, session_key);

    long extraNonceTmp = random();
    initNonce(fromNode, packetNum, extraNonceTmp);

    uint8_t *auth = bytesOut + numBytes;
    aes_ccm_ae(session_key, 32, nonce, 8, bytes, numBytes, nullptr, 0, bytesOut, auth);

    memcpy(auth + 8, &extraNonceTmp, sizeof(uint32_t));
    memcpy(auth + 12, ciphertext, CRYPTO_CIPHERTEXTBYTES);

    return true;
}


/**
 * Decrypt packet using Kyber KEM + AES  
 * This is called by the responder (receiver)
 */
bool CryptoEngine::decryptKyber(uint32_t fromNode, uint64_t packetNum, size_t numBytes, 
                               const uint8_t *bytes, uint8_t *bytesOut)
{
    if (!pq_keys_valid) {
        LOG_ERROR("No valid Kyber keys for decryption");
        return false;
    }

    size_t payload_len = numBytes - 12 - CRYPTO_CIPHERTEXTBYTES;
    const uint8_t *auth = bytes + payload_len;

    uint32_t extraNonce;
    memcpy(&extraNonce, auth + 8, sizeof(uint32_t));

    const uint8_t *ciphertext = auth + 12;

    uint8_t shared_secret[CRYPTO_BYTES];

    if (!kyber.decap(shared_secret,ciphertext, pq_private_key)) {
        LOG_ERROR("Kyber decapsulation failed with result");
        return false;
    }

    uint8_t session_key[32];
    deriveSessionKey(shared_secret, CRYPTO_BYTES, fromNode, 0, session_key);

    initNonce(fromNode, packetNum, extraNonce);

    LOG_DEBUG("Kyber decrypt: payload_len=%d, extraNonce=%d", payload_len, extraNonce);
    printBytes("Kyber session key: ", session_key, 8);

    return aes_ccm_ad(session_key, 32, nonce, 8, bytes, payload_len, nullptr, 0, auth, bytesOut);
}

/**
 * Derive session key from Kyber shared secret using HKDF-like construction
 */
void CryptoEngine::deriveSessionKey(const uint8_t *shared_secret, size_t secret_len, 
                                  uint32_t fromNode, uint32_t toNode, uint8_t *session_key)
{
    SHA256 hash;
    hash.reset();
    
    // Add shared secret
    hash.update(shared_secret, secret_len);
    
    // Add node IDs for key domain separation
    hash.update((uint8_t*)&fromNode, sizeof(fromNode));
    hash.update((uint8_t*)&toNode, sizeof(toNode));
    
    // Add some fixed salt
    const uint8_t salt[] = "MESHTASTIC_PQ_v1";
    hash.update(salt, sizeof(salt) - 1);
    
    hash.finalize(session_key, 32);
    
    LOG_DEBUG("Derived session key from %d-byte shared secret", secret_len);
}

/**
 * Clear all Kyber keys from memory
 */
void CryptoEngine::clearKyberKeys()
{
    memset(pq_public_key, 0, sizeof(pq_public_key));
    memset(pq_private_key, 0, sizeof(pq_private_key)); 
    pq_keys_valid = false;
    LOG_DEBUG("Cleared Kyber keys from memory");
}

/**
 * Check if we have valid Kyber keys
 */
bool CryptoEngine::hasValidKyberKeys() const
{
    return pq_keys_valid;
}

/**
 * Get the size of encrypted packet with Kyber overhead
 */
size_t CryptoEngine::getKyberPacketOverhead()
{
    return 12 + CRYPTO_CIPHERTEXTBYTES; // auth(8) + extra_nonce(4) + Kyber ciphertext
}

/**
 * Hybrid PQ+Classical encryption combining Kyber and Curve25519
 * This provides quantum resistance while maintaining backward compatibility
 */
bool CryptoEngine::encryptHybrid(uint32_t toNode, uint32_t fromNode, 
                                const uint8_t *classicalPubKey, const uint8_t *pqPubKey,
                                uint64_t packetNum, size_t numBytes, 
                                const uint8_t *bytes, uint8_t *bytesOut)
{
    LOG_DEBUG("Starting hybrid PQ+Classical encryption");
    
    if (!pq_keys_valid) {
        LOG_ERROR("No valid Kyber keys for hybrid encryption");
        return false;
    }
    
    // Step 1: Perform KYBER KEM
    uint8_t kyber_ciphertext[CRYPTO_CIPHERTEXTBYTES];
    uint8_t kyber_shared_secret[CRYPTO_BYTES];
    
    if (!kyber.encap(kyber_ciphertext, kyber_shared_secret, pqPubKey)) {
        LOG_ERROR("Kyber encapsulation failed in hybrid mode");
        return false;
    }
    
    // Step 2: Perform Classical Curve25519 ECDH 
    uint8_t classical_shared_secret[32];
    meshtastic_UserLite_public_key_t classical_key;
    classical_key.size = 32;
    memcpy(classical_key.bytes, classicalPubKey, 32);
    
    if (!setDHPublicKey(classical_key.bytes)) {
        LOG_ERROR("Classical ECDH failed in hybrid mode");
        return false;
    }
    hash(classical_shared_secret, 32);
    
    // Step 3: Combine both shared secrets using domain-separated hash
    uint8_t combined_secret[96]; // 32 + 32 + 32 for domain separation
    memcpy(combined_secret, kyber_shared_secret, CRYPTO_BYTES);
    memcpy(combined_secret + CRYPTO_BYTES, classical_shared_secret, 32);
    
    // Add domain separation
    const char *domain = "HYBRID_PQ_CLASSICAL_v1";
    memcpy(combined_secret + 64, domain, 32);
    
    // Step 4: Derive AES session key from combined secret
    uint8_t session_key[32];
    SHA256 hash;
    hash.update(combined_secret, sizeof(combined_secret));
    hash.update((uint8_t*)&fromNode, sizeof(fromNode));
    hash.update((uint8_t*)&toNode, sizeof(toNode));
    hash.finalize(session_key, 32);
    
    // Step 5: Encrypt the message with AES-CCM
    long extraNonceTmp = random();
    initNonce(fromNode, packetNum, extraNonceTmp);
    
    uint8_t *auth = bytesOut + numBytes;
    aes_ccm_ae(session_key, 32, nonce, 8, bytes, numBytes, nullptr, 0, bytesOut, auth);
    
    // Step 6: Append Kyber ciphertext and extra nonce
    memcpy(auth + 8, &extraNonceTmp, sizeof(uint32_t));
    memcpy(auth + 12, kyber_ciphertext, CRYPTO_CIPHERTEXTBYTES);
    
    LOG_INFO("Hybrid encryption completed: %zu bytes + %zu overhead", 
             numBytes, 12 + CRYPTO_CIPHERTEXTBYTES);
    
    return true;
}

/**
 * Hybrid PQ+Classical decryption
 */
bool CryptoEngine::decryptHybrid(uint32_t fromNode, uint64_t packetNum, 
                                size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut)
{
    LOG_DEBUG("Starting hybrid PQ+Classical decryption");
    
    if (!pq_keys_valid) {
        LOG_ERROR("No valid Kyber keys for hybrid decryption");
        return false;
    }
    
    size_t payload_len = numBytes - 12 - CRYPTO_CIPHERTEXTBYTES;
    const uint8_t *auth = bytes + payload_len;
    
    uint32_t extraNonce;
    memcpy(&extraNonce, auth + 8, sizeof(uint32_t));
    
    const uint8_t *kyber_ciphertext = auth + 12;
    
    // Step 1: Perform KYBER decapsulation
    uint8_t kyber_shared_secret[CRYPTO_BYTES];
    
    if (!kyber.decap(kyber_shared_secret, kyber_ciphertext, pq_private_key)) {
        LOG_ERROR("Kyber decapsulation failed in hybrid mode");
        return false;
    }
    
    // Step 2: Perform Classical Curve25519 ECDH with sender's stored public key
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(fromNode);
    if (!node || node->user.public_key.size != 32) {
        LOG_ERROR("No classical public key found for node 0x%x", fromNode);
        return false;
    }
    
    uint8_t classical_shared_secret[32];
    if (!setDHPublicKey(node->user.public_key.bytes)) {
        LOG_ERROR("Classical ECDH failed in hybrid decryption");
        return false;
    }
    hash(classical_shared_secret, 32);
    
    // Step 3: Combine both shared secrets (same as encryption)
    uint8_t combined_secret[96];
    memcpy(combined_secret, kyber_shared_secret, CRYPTO_BYTES);
    memcpy(combined_secret + CRYPTO_BYTES, classical_shared_secret, 32);
    
    const char *domain = "HYBRID_PQ_CLASSICAL_v1";
    memcpy(combined_secret + 64, domain, 32);
    
    // Step 4: Derive AES session key
    uint8_t session_key[32];
    SHA256 hash;
    uint32_t toNode = (uint32_t)nodeDB->getNodeNum();
    hash.update(combined_secret, sizeof(combined_secret));
    hash.update((uint8_t*)&fromNode, sizeof(fromNode));
    hash.update((uint8_t*)&toNode, sizeof(uint32_t));
    hash.finalize(session_key, 32);
    
    // Step 5: Decrypt with AES-CCM
    initNonce(fromNode, packetNum, extraNonce);
    
    bool success = aes_ccm_ad(session_key, 32, nonce, 8, bytes, payload_len, nullptr, 0, auth, bytesOut);
    
    if (success) {
        LOG_INFO("Hybrid decryption successful: %zu bytes", payload_len);
    } else {
        LOG_ERROR("Hybrid decryption failed");
    }
    
    return success;
}

#endif // not MESHTASTIC_EXCLUDE_PQ_CRYPTO

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
            LOG_ERROR("PKI key generation failed. Specified private key results in a weak");
            memset(pubKey, 0, 32);
            return false;
        }
        memcpy(private_key, privKey, sizeof(private_key));
        memcpy(public_key, pubKey, sizeof(public_key));
    } else {
        LOG_WARN("X25519 key generation failed due to blank private key");
        return false;
    }
    return true;
}
#endif

void CryptoEngine::clearKeys()
{
    memset(public_key, 0, sizeof(public_key));
    memset(private_key, 0, sizeof(private_key));
#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
    clearKyberKeys();
#endif
}

/**
 * Encrypt a packet's payload using a key generated with Curve25519 and SHA256
 * for a specific node.
 *
 * @param toNode The MeshPacket `to` field.
 * @param fromNode The MeshPacket `from` field.
 * @param remotePublic The remote node's Curve25519 public key.
 * @param packetId The MeshPacket `id` field.
 * @param numBytes Number of bytes of plaintext in the bytes buffer.
 * @param bytes Buffer containing plaintext input.
 * @param bytesOut Output buffer to be populated with encrypted ciphertext.
 */
bool CryptoEngine::encryptCurve25519(uint32_t toNode, uint32_t fromNode, meshtastic_UserLite_public_key_t remotePublic,
                                     uint64_t packetNum, size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut)
{
    uint8_t *auth;
    long extraNonceTmp = random();
    auth = bytesOut + numBytes;
    memcpy((uint8_t *)(auth + 8), &extraNonceTmp,
           sizeof(uint32_t)); // do not use dereference on potential non aligned pointers : *extraNonce = extraNonceTmp;
    LOG_DEBUG("Random nonce value: %d", extraNonceTmp);
    if (remotePublic.size == 0) {
        LOG_DEBUG("Node %d or their public_key not found", toNode);
        return false;
    }
    if (!crypto->setDHPublicKey(remotePublic.bytes)) {
        return false;
    }
    crypto->hash(shared_key, 32);
    initNonce(fromNode, packetNum, extraNonceTmp);

    // Calculate the shared secret with the destination node and encrypt
    printBytes("Attempt encrypt with nonce: ", nonce, 13);
    printBytes("Attempt encrypt with shared_key starting with: ", shared_key, 8);
    aes_ccm_ae(shared_key, 32, nonce, 8, bytes, numBytes, nullptr, 0, bytesOut,
               auth); // this can write up to 15 bytes longer than numbytes past bytesOut
    memcpy((uint8_t *)(auth + 8), &extraNonceTmp,
           sizeof(uint32_t)); // do not use dereference on potential non aligned pointers : *extraNonce = extraNonceTmp;
    return true;
}

/**
 * Decrypt a packet's payload using a key generated with Curve25519 and SHA256
 * for a specific node.
 *
 * @param fromNode The MeshPacket `from` field.
 * @param remotePublic The remote node's Curve25519 public key.
 * @param packetId The MeshPacket `id` field.
 * @param numBytes Number of bytes of ciphertext in the bytes buffer.
 * @param bytes Buffer containing ciphertext input.
 * @param bytesOut Output buffer to be populated with decrypted plaintext.
 */
bool CryptoEngine::decryptCurve25519(uint32_t fromNode, meshtastic_UserLite_public_key_t remotePublic, uint64_t packetNum,
                                     size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut)
{
    const uint8_t *auth = bytes + numBytes - 12; // set to last 8 bytes of text?
    uint32_t extraNonce;                         // pointer was not really used
    memcpy(&extraNonce, auth + 8,
           sizeof(uint32_t)); // do not use dereference on potential non aligned pointers : (uint32_t *)(auth + 8);
    LOG_INFO("Random nonce value: %d", extraNonce);

    if (remotePublic.size == 0) {
        LOG_DEBUG("Node or its public key not found in database");
        return false;
    }

    // Calculate the shared secret with the sending node and decrypt
    if (!crypto->setDHPublicKey(remotePublic.bytes)) {
        return false;
    }
    crypto->hash(shared_key, 32);

    initNonce(fromNode, packetNum, extraNonce);
    printBytes("Attempt decrypt with nonce: ", nonce, 13);
    printBytes("Attempt decrypt with shared_key starting with: ", shared_key, 8);
    return aes_ccm_ad(shared_key, 32, nonce, 8, bytes, numBytes - 12, nullptr, 0, auth, bytesOut);
}

void CryptoEngine::setDHPrivateKey(uint8_t *_private_key)
{
    memcpy(private_key, _private_key, 32);
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
    delete aes;
    aes = nullptr;
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
        LOG_WARN("Curve25519DH step 2 failed!");
        return false;
    }
    return true;
}

#endif
concurrency::Lock *cryptLock;

void CryptoEngine::setKey(const CryptoKey &k)
{
    LOG_DEBUG("Use AES%d key!", k.length * 8);
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
            LOG_ERROR("Packet too large for crypto engine: %d. noop encryption!", numBytes);
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
    delete ctr;
    ctr = nullptr;
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
