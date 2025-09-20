#include "CryptoEngine.h"
// #include "NodeDB.h"
#include "architecture.h"

#if !(MESHTASTIC_EXCLUDE_PKI)
#include "NodeDB.h"
#include "XEdDSA.h"
#include "aes-ccm.h"
#include "meshUtils.h"
#include <Crypto.h>
#include <Curve25519.h>
#include <Ed25519.h>
#include <RNG.h>
#include <SHA256.h>

#ifndef NUM_LIMBS_256BIT
#define NUM_LIMBS_BITS(n) (((n) + sizeof(limb_t) * 8 - 1) / (8 * sizeof(limb_t)))
#define NUM_LIMBS_256BIT NUM_LIMBS_BITS(256)
#endif

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN)

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
    XEdDSA::priv_curve_to_ed_keys(private_key, xeddsa_private_key, xeddsa_public_key);
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
            LOG_ERROR("PKI key generation failed. Specified private key results in a weak");
            memset(pubKey, 0, 32);
            return false;
        }
        memcpy(private_key, privKey, sizeof(private_key));
        memcpy(public_key, pubKey, sizeof(public_key));
        XEdDSA::priv_curve_to_ed_keys(private_key, xeddsa_private_key, xeddsa_public_key);
    } else {
        LOG_WARN("X25519 key generation failed due to blank private key");
        return false;
    }
    return true;
}

bool CryptoEngine::xeddsa_sign(uint8_t *message, size_t len, uint8_t *signature)
{
    XEdDSA::sign(signature, xeddsa_private_key, xeddsa_public_key, message,
                 len); // sign will need modified to use the raw secret scalar, and not hash it first.
    return true;
}
bool CryptoEngine::xeddsa_verify(uint8_t *pubKey, uint8_t *message, size_t len, uint8_t *signature)
{
    uint8_t publicKey[32] = {0};
    curve_to_ed_pub(pubKey, publicKey);

    return XEdDSA::verify(signature, publicKey, message, len);
}

void CryptoEngine::curve_to_ed_pub(uint8_t *curve_pubkey, uint8_t *ed_pubkey)
{

    // Apply the birational map defined in RFC 7748, section 4.1 "Curve25519" to calculate an Ed25519 public
    // key from a Curve25519 public key. Because the serialization format of Curve25519 public keys only
    // contains the u coordinate, the x coordinate of the corresponding Ed25519 public key can't be uniquely
    // calculated as defined by the birational map. The x coordinate is represented in the serialization
    // format of Ed25519 public keys only in a single sign bit. This function assumes that the sign bit is
    // known to the user and is passed accordingly.
    fe u, y;
    fe one;
    fe u_minus_one, u_plus_one, u_plus_one_inv;

    // Parse the Curve25519 public key input as a field element containing the u coordinate. RFC 7748,
    // section 5 "The X25519 and X448 Functions", mandates that the most significant bit of the Curve25519
    // public key has to be zeroized. This is handled by fe_frombytes internally.
    fe_frombytes(u, curve_pubkey);

    // Calculate the parameters (u - 1) and (u + 1)
    fe_1(one);
    fe_sub(u_minus_one, u, one);
    fe_add(u_plus_one, u, one);

    // Invert u + 1
    fe_invert(u_plus_one_inv, u_plus_one);

    // Calculate y = (u - 1) * inv(u + 1) (mod p)
    fe_mul(y, u_minus_one, u_plus_one_inv);

    // Serialize the field element containing the y coordinate to the Ed25519 public key output
    fe_tobytes(ed_pubkey, y);

    // Set the sign bit to zero
    ed_pubkey[31] &= 0x7f;

    // need to convert the pubkey y = ( u - 1) * inv( u + 1) (mod p).
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
