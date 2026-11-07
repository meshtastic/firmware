#include "CryptoEngine.h"
#include "architecture.h"
#include "crypto/crypto_api.h"

#if !(MESHTASTIC_EXCLUDE_PKI)
#include "HardwareRNG.h"
#include "NodeDB.h"
#include "meshUtils.h"
#include <RNG.h>

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

    uint8_t hardwareEntropy[64] = {0};
    if (HardwareRNG::fill(hardwareEntropy, sizeof(hardwareEntropy), true)) {
        crypto_random_add(hardwareEntropy, sizeof(hardwareEntropy));
    } else {
        LOG_WARN("Hardware entropy unavailable, falling back to software RNG");
    }
    memset(hardwareEntropy, 0, sizeof(hardwareEntropy));

    if (myNodeInfo.device_id.size == 16) {
        crypto_random_add(myNodeInfo.device_id.bytes, myNodeInfo.device_id.size);
    }
    auto noise = random();
    crypto_random_add((uint8_t *)&noise, sizeof(noise));

    LOG_DEBUG("Generate Curve25519 keypair");
    crypto_random(private_key, sizeof(private_key));
    private_key[0] &= 0xF8;
    private_key[31] = (private_key[31] & 0x7F) | 0x40;
    crypto_x25519(public_key, private_key, nullptr);
    memcpy(pubKey, public_key, sizeof(public_key));
    memcpy(privKey, private_key, sizeof(private_key));
    clearSharedSecretCache();
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    crypto_xeddsa_keys_from_x25519(xeddsa_private_key, xeddsa_public_key, private_key);
#endif
}

/**
 * regenerate a public key with Curve25519.
 *
 * @param pubKey The destination for the public key.
 * @param privKey The source for the private key, clamped in place.
 */
bool CryptoEngine::regeneratePublicKey(uint8_t *pubKey, uint8_t *privKey)
{
    if (!memfll(privKey, 0, sizeof(private_key))) {
        // Clamp in place so the stored key is the one x25519 and the XEdDSA key
        // both derive from.
        const uint8_t first = privKey[0], last = privKey[31];
        privKey[0] &= 0xF8;
        privKey[31] = (privKey[31] & 0x7F) | 0x40;
        if (privKey[0] != first || privKey[31] != last)
            LOG_WARN("Private key was not clamped; its public key and node number change");
        crypto_x25519(pubKey, privKey, nullptr);
        if (crypto_x25519_is_weak(pubKey)) {
            LOG_ERROR("PKI key generation failed. Specified private key results in a weak");
            memset(pubKey, 0, 32);
            return false;
        }
        memcpy(private_key, privKey, sizeof(private_key));
        memcpy(public_key, pubKey, sizeof(public_key));
        clearSharedSecretCache();
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
        crypto_xeddsa_keys_from_x25519(xeddsa_private_key, xeddsa_public_key, private_key);
#endif
    } else {
        LOG_WARN("X25519 key generation failed due to blank private key");
        return false;
    }
    return true;
}

/** Write a little-endian uint32 - the encoding is pinned by the protocol, not by the host. */
static void putLE32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v & 0xff);
    out[1] = (uint8_t)((v >> 8) & 0xff);
    out[2] = (uint8_t)((v >> 16) & 0xff);
    out[3] = (uint8_t)((v >> 24) & 0xff);
}

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
/**
 * Build the buffer a signature covers. One fixed layout, always:
 *
 *   version(1) | from(4) | id(4) | to(4) | portnum(4) | request_id(4) | reply_id(4)
 *             | emoji(4) | bitfield(4) | flags(1) | payload(N)
 *
 * Everything in the Data envelope is covered, not just Data.payload. Those envelope fields ride
 * outside the payload, and channel crypto is AES-CTR with no MAC, so anything left out of here is
 * rewritable in flight by any PSK holder while the signature still verifies:
 *
 *  - reply_id: re-points a signed reply or tapback at a different message.
 *  - emoji: turns a signed reply into a signed reaction, or the reverse. Binding reply_id without
 *    it leaves the reaction attack half-open, so the two belong together.
 *  - bitfield: carries OK_TO_MQTT, the sender's consent to being uploaded to a public broker, and
 *    the exploitable direction (0 -> 1) is the one that leaks. The whole uint32 is covered rather
 *    than the two defined bits, so bits 2..31 are protected before anything uses them.
 *  - want_response: bit 1 of bitfield mirrors it and Router.cpp merges them with |=, so signing one
 *    without the other protects neither. Both, in the flags byte.
 *  - to: without it a signed broadcast can be re-addressed as a direct message and still verify,
 *    delivering a public statement as an apparent private one. Relays rewrite hop_limit, next_hop
 *    and relay_node, never `to`, so it is stable end to end.
 *  - request_id: carried by signed replies - NodeInfo and position replies, and replies in ham mode.
 *
 * Deliberately NOT covered: dest and source (one write in the tree, no readers), channel (the wire
 * carries a hash where the decoded packet carries an index), and the hop fields, which relays
 * rewrite by design. Data.payload IS covered, and always was - note that this makes TRACEROUTE_APP
 * unsignable, since every hop rewrites the RouteDiscovery in place.
 *
 * Fixed-length header, so the payload boundary is total - XEDDSA_SIGNED_HEADER_LEN and never
 * depends on content. That is what makes the encoding unambiguous: with a conditional layout, a
 * payload could be re-split into header fields to produce identical bytes, letting an attacker move
 * payload bytes into request_id/reply_id and truncate a signed message without breaking it.
 *
 * Integers are little-endian explicitly, so the value is a property of the protocol rather than of
 * the compiler that built the node.
 *
 * Covering the metadata also prevents replay, reattribution, and portnum redirection.
 */
static size_t buildSigningBuffer(uint8_t *buf, size_t bufSize, uint32_t fromNode, uint32_t packetId, uint32_t toNode,
                                 const meshtastic_Data *d)
{
    if (!d)
        return 0;
    const size_t totalLen = XEDDSA_SIGNED_HEADER_LEN + d->payload.size;
    if (totalLen > bufSize)
        return 0;

    uint8_t *w = buf;
    *w++ = XEDDSA_SIGNING_VERSION;
    putLE32(w, fromNode);
    w += sizeof(uint32_t);
    putLE32(w, packetId);
    w += sizeof(uint32_t);
    putLE32(w, toNode);
    w += sizeof(uint32_t);
    putLE32(w, (uint32_t)d->portnum);
    w += sizeof(uint32_t);
    putLE32(w, d->request_id);
    w += sizeof(uint32_t);
    putLE32(w, d->reply_id);
    w += sizeof(uint32_t);
    putLE32(w, d->emoji);
    w += sizeof(uint32_t);
    // Absent bitfield signs as zero; its presence is carried in the flags byte, so stripping the
    // field is not the same as sending it empty.
    putLE32(w, d->has_bitfield ? d->bitfield : 0);
    w += sizeof(uint32_t);
    *w++ = (uint8_t)((d->want_response ? XEDDSA_SIGNED_FLAG_WANT_RESPONSE : 0) |
                     (d->has_bitfield ? XEDDSA_SIGNED_FLAG_HAS_BITFIELD : 0));

    if (d->payload.size)
        memcpy(w, d->payload.bytes, d->payload.size);
    return totalLen;
}

bool CryptoEngine::xeddsa_sign(uint32_t fromNode, uint32_t packetId, uint32_t toNode, const meshtastic_Data *d,
                               uint8_t *signature)
{
    if (memfll(xeddsa_private_key, 0, sizeof(xeddsa_private_key)))
        return false;
    uint8_t sigBuf[XEDDSA_SIGN_BUF_LEN];
    size_t sigLen = buildSigningBuffer(sigBuf, sizeof(sigBuf), fromNode, packetId, toNode, d);
    if (sigLen == 0)
        return false;
    return crypto_xeddsa_sign(signature, xeddsa_private_key, xeddsa_public_key, sigBuf, sigLen);
}

bool CryptoEngine::xeddsa_verify(const uint8_t *pubKey, uint32_t fromNode, uint32_t packetId, uint32_t toNode,
                                 const meshtastic_Data *d, const uint8_t *signature)
{
    // Use cached Ed25519 key if the Curve25519 key matches, avoiding expensive field inversion
    if (memcmp(pubKey, cached_curve_pubkey, 32) != 0) {
        crypto_xeddsa_pub_from_x25519(cached_ed_pubkey, pubKey);
        memcpy(cached_curve_pubkey, pubKey, 32);
    }
    alignas(4) uint8_t sigBuf[XEDDSA_SIGN_BUF_LEN]; // aligned: hardware verifiers DMA it in place
    size_t sigLen = buildSigningBuffer(sigBuf, sizeof(sigBuf), fromNode, packetId, toNode, d);
    if (sigLen == 0)
        return false;
    return crypto_ed25519_verify(signature, cached_ed_pubkey, sigBuf, sigLen);
}

#endif

bool CryptoEngine::ensurePkiKeys(meshtastic_Config_SecurityConfig &security, meshtastic_User &user)
{
    if (user.is_licensed) {
        return false;
    }

    bool keygenSuccess = false;
    if (security.private_key.size == 32) {
        if (regeneratePublicKey(security.public_key.bytes, security.private_key.bytes)) {
            keygenSuccess = true;
        }
    } else {
        LOG_INFO("Generate new PKI keys");
        generateKeyPair(security.public_key.bytes, security.private_key.bytes);
        // A hardware error leaves an all-zero public key, which must not be stored as an identity.
        keygenSuccess = !memfll(security.public_key.bytes, 0, 32);
    }

    if (keygenSuccess) {
        security.public_key.size = 32;
        security.private_key.size = 32;
        user.public_key.size = 32;
        memcpy(user.public_key.bytes, security.public_key.bytes, 32);
    }

    return keygenSuccess;
}
#endif

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
bool CryptoEngine::encryptCurve25519(uint32_t toNode, uint32_t fromNode, meshtastic_NodeInfoLite_public_key_t remotePublic,
                                     uint64_t packetNum, size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut)
{
    uint8_t *auth;
    // The extra nonce must be unpredictable: use the hardware RNG, falling back to the
    // seeded CSPRNG only when no hardware source is available.
    uint32_t extraNonceTmp;
    crypto_random((uint8_t *)&extraNonceTmp, sizeof(extraNonceTmp));
    auth = bytesOut + numBytes;
    if (remotePublic.size == 0) {
        LOG_DEBUG("Node %d or their public_key not found", toNode);
        return false;
    }
    if (!setCryptoSharedSecret(remotePublic.bytes)) {
        return false;
    }
    initNonce(fromNode, packetNum, extraNonceTmp);

    // Calculate the shared secret with the destination node and encrypt
    if (!crypto_aes_ccm_seal(shared_key, 32, nonce, nullptr, 0, bytes, numBytes, bytesOut, auth, 8))
        return false;
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
bool CryptoEngine::decryptCurve25519(uint32_t fromNode, meshtastic_NodeInfoLite_public_key_t remotePublic, uint64_t packetNum,
                                     size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut)
{
    if (numBytes < 12)
        return false;
    const uint8_t *auth = bytes + numBytes - 12;
    uint32_t extraNonce; // pointer was not really used
    memcpy(&extraNonce, auth + 8,
           sizeof(uint32_t)); // do not use dereference on potential non aligned pointers : (uint32_t *)(auth + 8);
    if (remotePublic.size == 0) {
        LOG_DEBUG("Node or its public key not found in database");
        return false;
    }

    // Calculate the shared secret with the sending node and decrypt
    if (!setCryptoSharedSecret(remotePublic.bytes)) {
        return false;
    }

    initNonce(fromNode, packetNum, extraNonce);
    return crypto_aes_ccm_open(shared_key, 32, nonce, nullptr, 0, bytes, numBytes - 12, auth, 8, bytesOut);
}

// The label is domain separation only - it never needs to be secret. It is fixed length and
// precedes the fixed-width fields, and only the trailing Routing bytes are variable, so the input is
// unambiguous without length prefixes.
static const char ACK_PROOF_LABEL[] = "ack";
#define ACK_PROOF_LABEL_LEN (sizeof(ACK_PROOF_LABEL) - 1) // no trailing NUL on the wire

bool CryptoEngine::ackProofCompute(const uint8_t *peerPubKey, uint32_t ackFrom, uint32_t ackTo, uint32_t requestId,
                                   const uint8_t *routing, size_t routingLen, uint8_t *proofOut)
{
    if (memfll(private_key, 0, sizeof(private_key)))
        return false; // no identity yet - nothing to prove with

    // Same derivation, and the same cache, encryptCurve25519/decryptCurve25519 use.
    if (!setCryptoSharedSecret(peerPubKey))
        return false; // includes the library's weak-point check on a cache miss

    uint8_t header[ACK_PROOF_LABEL_LEN + 3 * sizeof(uint32_t)];
    memcpy(header, ACK_PROOF_LABEL, ACK_PROOF_LABEL_LEN);
    putLE32(header + ACK_PROOF_LABEL_LEN, ackFrom);
    putLE32(header + ACK_PROOF_LABEL_LEN + 4, ackTo);
    putLE32(header + ACK_PROOF_LABEL_LEN + 8, requestId);

    const crypto_part parts[] = {{header, sizeof(header)}, {routing, routing ? routingLen : 0}};
    uint8_t digest[32];
    crypto_hmac_sha256(shared_key, 32, parts, 2, digest);
    memcpy(proofOut, digest, ACK_PROOF_SIZE);

    memset(digest, 0, sizeof(digest));
    memset(shared_key, 0, sizeof(shared_key)); // clear the working copy; the cache keeps its own
    return true;
}

void CryptoEngine::setDHPrivateKey(uint8_t *_private_key)
{
    memcpy(private_key, _private_key, 32);
    clearSharedSecretCache();
}

void CryptoEngine::clearSharedSecretCache()
{
    memset(sharedSecretCache, 0, sizeof(sharedSecretCache));
}

/**
 * Derive shared_key = SHA256(X25519(our private, peer public)), reusing the cached result when we
 * have derived it for this peer before. Entries are matched on the whole peer key: a shorter tag
 * can be ground out by an impostor, who would then receive traffic meant for the peer it shadows.
 */
bool CryptoEngine::setCryptoSharedSecret(const uint8_t *peerPubKey)
{
    // The last used timestamp is in units of ~1.165 hours, which gives us
    // ~12.3 days before the timestamps roll over. This is ok since a periodic
    // misfire on evicting the oldest secret has very little impact.
    const uint8_t now = (millis() >> 22) & 0xff;

    uint16_t oldestDelta = 0;
    size_t oldestIndex = 0;
    bool haveEmptySlot = false;
    for (size_t i = 0; i < MAX_CACHED_SHARED_SECRETS; i++) {
        CachedSharedSecret &entry = sharedSecretCache[i];
        if (entry.valid && memcmp(entry.peer_public_key, peerPubKey, 32) == 0) {
            // Cache hit! Copy it into shared_key.
            memcpy(shared_key, entry.shared_secret, 32);
            // Update the last used timestamp
            entry.last_used = now;
            return true;
        }

        if (haveEmptySlot) {
            // We already have a slot to insert into. Keep looking for a cache hit.
            continue;
        }

        if (!entry.valid) {
            // This entry is empty. We can insert into it later, if needed.
            oldestIndex = i;
            haveEmptySlot = true;
            continue;
        }

        // Track the oldest entry in case the cache is full.
        uint16_t delta = 0;
        if (now >= entry.last_used) {
            delta = now - entry.last_used;
        } else {
            // Assume a larger last used timestamp is further in the past
            delta = uint16_t(0x100) + now - entry.last_used;
        }
        if (delta > oldestDelta) {
            oldestIndex = i;
            oldestDelta = delta;
        }
    }

    // Cache miss. Generate the shared secret.
    if (!setDHPublicKey(peerPubKey)) {
        return false;
    }
    hash(shared_key, 32);

    // Insert the calculated shared secret into the cache, overwriting an old entry if needed.
    CachedSharedSecret &oldestEntry = sharedSecretCache[oldestIndex];
    memcpy(oldestEntry.peer_public_key, peerPubKey, 32);
    oldestEntry.last_used = now;
    oldestEntry.valid = true;
    memcpy(oldestEntry.shared_secret, shared_key, 32);
    return true;
}

#endif // !(MESHTASTIC_EXCLUDE_PKI)

/**
 * Hash arbitrary data using SHA256.
 *
 * @param bytes
 * @param numBytes
 */
void CryptoEngine::hash(uint8_t *bytes, size_t numBytes)
{
    const crypto_part part = {bytes, numBytes};
    crypto_sha256(&part, 1, bytes);
}

#if !(MESHTASTIC_EXCLUDE_PKI)

bool CryptoEngine::setDHPublicKey(const uint8_t *pubKey)
{
    // Same checks as Curve25519::dh2 (rejects e.g. an all-0 public or shared
    // key); all of them run so timing doesn't depend on which one fails.
    uint8_t weak = crypto_x25519_is_weak(pubKey);
    weak |= crypto_x25519(shared_key, private_key, pubKey) ? 0 : 1;
    weak |= crypto_x25519_is_weak(shared_key);
    if (weak) {
        LOG_WARN("Curve25519DH step 2 failed");
        return false;
    }
    return true;
}

void CryptoEngine::setPendingPublicKey(uint32_t node, const uint8_t *key)
{
    concurrency::LockGuard g(&pendingKeyLock);
    pendingKeyVerificationNode = node;
    memcpy(pendingKeyVerificationPublicKey, key, 32);
    hasPendingKeyVerificationKey = true;
}

void CryptoEngine::clearPendingPublicKey()
{
    concurrency::LockGuard g(&pendingKeyLock);
    pendingKeyVerificationNode = 0;
    memset(pendingKeyVerificationPublicKey, 0, 32);
    hasPendingKeyVerificationKey = false;
}

bool CryptoEngine::getPendingPublicKey(uint32_t node, meshtastic_NodeInfoLite_public_key_t &out)
{
    concurrency::LockGuard g(&pendingKeyLock);
    if (!hasPendingKeyVerificationKey || node == 0 || node != pendingKeyVerificationNode)
        return false;
    out.size = 32;
    memcpy(out.bytes, pendingKeyVerificationPublicKey, 32);
    return true;
}

#endif

// AAD layout: [fromNode (4)] [toNode (4)], in the same native byte order initNonce uses.
static void initAad(uint32_t fromNode, uint32_t toNode, uint8_t *aad)
{
    // memcpy to avoid breaking strict-aliasing, as initNonce does
    memcpy(aad, &fromNode, sizeof(uint32_t));
    memcpy(aad + sizeof(uint32_t), &toNode, sizeof(uint32_t));
}

bool CryptoEngine::encryptPacketCCM(const CryptoKey &psk, uint32_t fromNode, uint32_t toNode, uint64_t packetId, size_t numBytes,
                                    const uint8_t *plaintext, uint8_t *ciphertextWithTag)
{
    // length is int8_t and the aes_ccm_* key length is size_t, so the -1 "invalid key"
    // sentinel would widen into a huge unsigned length rather than being rejected.
    if (psk.length <= 0) {
        LOG_ERROR("AEAD encryption requires a valid, non-empty PSK");
        return false;
    }
    initNonce(fromNode, packetId);
    uint8_t aad[AEAD_AAD_SIZE];
    initAad(fromNode, toNode, aad);
    // Output layout: [ciphertext (numBytes)] [auth_tag (AEAD_TAG_SIZE bytes)]
    return crypto_aes_ccm_seal(psk.bytes, psk.length, nonce, aad, sizeof(aad), plaintext, numBytes, ciphertextWithTag,
                               ciphertextWithTag + numBytes, AEAD_TAG_SIZE);
}

bool CryptoEngine::decryptPacketCCM(const CryptoKey &psk, uint32_t fromNode, uint32_t toNode, uint64_t packetId,
                                    size_t totalBytes, const uint8_t *ciphertextWithTag, uint8_t *plaintext)
{
    if (psk.length <= 0) {
        LOG_ERROR("AEAD decryption requires a valid, non-empty PSK");
        return false;
    }
    if (totalBytes <= AEAD_TAG_SIZE)
        return false;
    initNonce(fromNode, packetId);
    uint8_t aad[AEAD_AAD_SIZE];
    initAad(fromNode, toNode, aad);
    size_t crypt_len = totalBytes - AEAD_TAG_SIZE;
    const uint8_t *auth = ciphertextWithTag + crypt_len;
    return crypto_aes_ccm_open(psk.bytes, psk.length, nonce, aad, sizeof(aad), ciphertextWithTag, crypt_len, auth, AEAD_TAG_SIZE,
                               plaintext);
}

concurrency::Lock *cryptLock;

void CryptoEngine::setKey(const CryptoKey &k)
{
    LOG_DEBUG("Use AES%d key", k.length * 8);
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
            LOG_ERROR("Packet too large for crypto engine: %d. noop encryption", numBytes);
        }
    }
}

void CryptoEngine::decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes)
{
    // For CTR, the implementation is the same
    encryptPacket(fromNode, packetId, numBytes, bytes);
}

void CryptoEngine::encryptAESCtr(CryptoKey _key, uint8_t *_nonce, size_t numBytes, uint8_t *bytes)
{
    if (_key.length > 0 && !crypto_aes_ctr(_key.bytes, _key.length, _nonce, bytes, numBytes, bytes))
        LOG_ERROR("AES-CTR failed, key length %d", _key.length);
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
CryptoEngine *crypto = new CryptoEngine;
