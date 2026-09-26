#pragma once
#include "AES.h"
#include "CTR.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include <Arduino.h>
#include <memory>

extern concurrency::Lock *cryptLock;

struct CryptoKey {
    uint8_t bytes[32];

    /// # of bytes, or -1 to mean "invalid key - do not use"
    int8_t length;
};

/**
 * see docs/software/crypto.md for details.
 *
 */

#if !(MESHTASTIC_EXCLUDE_PKI)
struct CachedSharedSecret {
    uint32_t lookup_key;
    uint8_t shared_secret[32];
    uint8_t last_used;
    // An explicit flag, rather than lookup_key == 0 meaning empty: a peer key whose first 4 bytes
    // are zero - the all-zero weak key among them - would otherwise match every unused slot and be
    // served an all-zero secret as a hit. Costs nothing, the struct is padded to the same size.
    bool valid;
};

/**
 * Max number of cached secrets to track. This should be roughly dependent on MAX_NUM_NODES but
 * cannot be directly because it is not a constant expression.
 */
#if defined(ARCH_STM32WL)
#define MAX_CACHED_SHARED_SECRETS 2
#elif defined(ARCH_NRF52)
#define MAX_CACHED_SHARED_SECRETS 8
#else
#define MAX_CACHED_SHARED_SECRETS 10
#endif
#endif

#define MAX_BLOCKSIZE 256
#define TEST_CURVE25519_FIELD_OPS // Exposes Curve25519::isWeakPoint() for testing keys
#define XEDDSA_SIGNATURE_SIZE 64
// Encoded size the signature adds to the Data protobuf: 1 tag byte (field 10 < 16) +
// 1 length byte (64 < 128) + 64 signature bytes. test_packet_signing asserts this stays exact.
#define XEDDSA_SIGNATURE_FIELD_BYTES (XEDDSA_SIGNATURE_SIZE + 2)
// Length of Routing.ack_proof, taken from the generated field so the protocol owns the number.
static constexpr size_t ACK_PROOF_SIZE = sizeof(meshtastic_Routing_ack_proof_t::bytes);

// Signing-buffer format. Bump this if the covered fields or their order ever change: it makes a
// buffer built by one version impossible to reinterpret as one built by another.
#define XEDDSA_SIGNING_VERSION 0x01
// version(1) | from | id | to | portnum | request_id | reply_id | emoji | bitfield | flags(1)
static constexpr size_t XEDDSA_SIGNED_HEADER_LEN = 1 + 8 * sizeof(uint32_t) + 1;
// The signing buffer is local scratch and is never transmitted, so no wire limit applies to it.
// Sized against the largest payload the Data schema can hold rather than against what the sender's
// fits-on-air gate currently admits, so buildSigningBuffer cannot run out of room on a well-formed
// packet however that gate is later tuned - an overflow there would silently stop signing.
static constexpr size_t XEDDSA_SIGN_BUF_LEN = XEDDSA_SIGNED_HEADER_LEN + meshtastic_Constants_DATA_PAYLOAD_LEN;
// Bit positions in the signing buffer's flags byte.
#define XEDDSA_SIGNED_FLAG_WANT_RESPONSE 0x01
#define XEDDSA_SIGNED_FLAG_HAS_BITFIELD 0x02

class CryptoEngine
{
  public:
#if !(MESHTASTIC_EXCLUDE_PKI)
    uint8_t public_key[32] = {0};
#endif

    virtual ~CryptoEngine() {}
#if !(MESHTASTIC_EXCLUDE_PKI)
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN)
    virtual void generateKeyPair(uint8_t *pubKey, uint8_t *privKey);
    virtual bool regeneratePublicKey(uint8_t *pubKey, uint8_t *privKey);
    virtual bool ensurePkiKeys(meshtastic_Config_SecurityConfig &security, meshtastic_User &user);
#endif
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    // The whole Data envelope is covered, not just its payload - see buildSigningBuffer. Takes the
    // Data rather than a field list so adding a field to the covered set cannot silently miss a
    // call site. toNode and fromNode come from the MeshPacket header; everything else is in `d`.
    bool xeddsa_sign(uint32_t fromNode, uint32_t packetId, uint32_t toNode, const meshtastic_Data *d, uint8_t *signature);
    bool xeddsa_verify(const uint8_t *pubKey, uint32_t fromNode, uint32_t packetId, uint32_t toNode, const meshtastic_Data *d,
                       const uint8_t *signature);
#endif
    /**
     * Derive the pairwise ACK proof carried in Routing.ack_proof.
     *
     *   proof = HMAC-SHA256( sharedKey,
     *                        "ack" | LE32(ackFrom) | LE32(ackTo) | LE32(requestId) | routing )
     *           [0 .. ACK_PROOF_SIZE)
     *
     * sharedKey is the same SHA256(X25519(our private, peer public)) packet crypto uses, so only the
     * two endpoints can produce or check it. What each input is for:
     *
     *  - ackFrom / ackTo: X25519 is symmetric, so DH(a_priv, B_pub) == DH(b_priv, A_pub). Without
     *    the direction bound, an A->B proof for a requestId equals the B->A proof for it.
     *  - requestId: stops a captured proof being retargeted at another outstanding packet.
     *  - routing: the encoded Routing message WITHOUT ack_proof, as the bytes arrived. An ack and a
     *    nak for one packet otherwise hash identically, and channel crypto is CTR with no integrity
     *    check, so a PSK holder could flip a proven success into a failure and it would still verify.
     *  - HMAC rather than SHA256(key | msg): the raw construction is not breakable here, but HMAC is
     *    the one with a proof behind it and costs no flash - SHA256 already carries resetHMAC and
     *    finalizeHMAC in its vtable.
     *  - Integers are little-endian explicitly, so the value is a property of the protocol and not of
     *    the compiler that built the node.
     *
     * Cost: one X25519 per peer whose derived key is not in the shared-secret cache, which an
     * attacker chooses when we pay by sending a forged ack from an unseen key.
     * Callers MUST gate on cheap checks first - see ReliableRouter::ackProofPermitsAction.
     *
     * Clobbers shared_key, so the caller must hold cryptLock (which is NOT recursive - do not call
     * this from a context that already holds it, such as perhapsEncode).
     */
    bool ackProofCompute(const uint8_t *peerPubKey, uint32_t ackFrom, uint32_t ackTo, uint32_t requestId, const uint8_t *routing,
                         size_t routingLen, uint8_t *proofOut);

    void setDHPrivateKey(uint8_t *_private_key);
    // The remotePublic key parameter takes the public_key bytes container from
    // a stored node header. NodeInfoLite is the on-device storage type since
    // the slim refactor flattened UserLite into it.
    virtual bool encryptCurve25519(uint32_t toNode, uint32_t fromNode, meshtastic_NodeInfoLite_public_key_t remotePublic,
                                   uint64_t packetNum, size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut);
    virtual bool decryptCurve25519(uint32_t fromNode, meshtastic_NodeInfoLite_public_key_t remotePublic, uint64_t packetNum,
                                   size_t numBytes, const uint8_t *bytes, uint8_t *bytesOut);
    virtual bool setDHPublicKey(uint8_t *publicKey);

    // Temporary holder for a peer's not-yet-verified public key, learned in-band during an
    // in-progress key-verification handshake before it is committed to NodeDB. Lets the Router
    // run the DH handshake to encode/decode the follow-on PKI packet. Single slot is enough:
    // only one verification runs at a time. Discarded when the handshake ends (resetToIdle).
    // Internally guarded by pendingKeyLock, not cryptLock: the Router calls the getter while
    // already holding the non-recursive cryptLock; KeyVerificationModule writes from elsewhere.
    void setPendingPublicKey(uint32_t node, const uint8_t *key);
    void clearPendingPublicKey();
    // Fills `out` (size set to 32) and returns true iff a pending key is held for `node`.
    bool getPendingPublicKey(uint32_t node, meshtastic_NodeInfoLite_public_key_t &out);
#endif

    // Plain SHA256; outside the guard because PortduinoGlue uses it on EXCLUDE_PKI builds.
    virtual void hash(uint8_t *bytes, size_t numBytes);

    virtual void aesSetKey(const uint8_t *key, size_t key_len);

    virtual void aesEncrypt(uint8_t *in, uint8_t *out);
    std::unique_ptr<BlockCipher> aes = nullptr;

    static constexpr size_t AEAD_TAG_SIZE = 12;
    // Sender and destination IDs are authenticated as associated data: the nonce already binds
    // `from` and the packet id, and the hop fields are left out because relays rewrite them.
    static constexpr size_t AEAD_AAD_SIZE = 2 * sizeof(uint32_t);

    virtual bool encryptPacketCCM(const CryptoKey &psk, uint32_t fromNode, uint32_t toNode, uint64_t packetId, size_t numBytes,
                                  const uint8_t *plaintext, uint8_t *ciphertextWithTag);

    virtual bool decryptPacketCCM(const CryptoKey &psk, uint32_t fromNode, uint32_t toNode, uint64_t packetId, size_t totalBytes,
                                  const uint8_t *ciphertextWithTag, uint8_t *plaintext);

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a _static_ buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(const CryptoKey &k);

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encryptPacket(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);
    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);
    virtual void encryptAESCtr(CryptoKey key, uint8_t *nonce, size_t numBytes, uint8_t *bytes);
#ifndef PIO_UNIT_TESTING
  protected:
#endif
    /** Our per packet nonce */
    uint8_t nonce[16] = {0};
    CryptoKey key = {};
#if !(MESHTASTIC_EXCLUDE_PKI)
    uint8_t shared_key[32] = {0};
    uint8_t private_key[32] = {0};
    uint32_t pendingKeyVerificationNode = 0;
    uint8_t pendingKeyVerificationPublicKey[32] = {0};
    bool hasPendingKeyVerificationKey = false;
    concurrency::Lock pendingKeyLock;
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    uint8_t xeddsa_public_key[32] = {0};
    uint8_t xeddsa_private_key[32] = {0};
    void curve_to_ed_pub(const uint8_t *curve_pubkey, uint8_t *ed_pubkey);
    // Single-entry cache for curve_to_ed_pub conversion (avoids expensive field inversion per packet)
    uint8_t cached_curve_pubkey[32] = {0};
    uint8_t cached_ed_pubkey[32] = {0};
#endif

    /**
     * Cache mapping peers' public keys -> {shared_secret, last_used}
     */
    CachedSharedSecret sharedSecretCache[MAX_CACHED_SHARED_SECRETS] = {};

    /**
     * Set cryptographic (hashed) shared_key calculated from the given peer public key, deriving it
     * only on a cache miss. Caller must hold cryptLock, as with setDHPublicKey.
     */
    bool setCryptoSharedSecret(const uint8_t *peerPubKey);

    /** Drop every cached secret. Called whenever our own private key changes: they are all stale. */
    void clearSharedSecretCache();
#endif
    /**
     * Init our 128 bit nonce for a new packet
     *
     * The NONCE is constructed by concatenating (from MSB to LSB):
     * a 64 bit packet number (stored in little endian order)
     * a 32 bit sending node number (stored in little endian order)
     * a 32 bit block counter (starts at zero)
     */
    void initNonce(uint32_t fromNode, uint64_t packetId, uint32_t extraNonce = 0);
};

extern CryptoEngine *crypto;