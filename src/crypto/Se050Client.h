#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_I2C

#include <Wire.h>
#include <stddef.h>
#include <stdint.h>

namespace se050
{

constexpr size_t UID_SIZE = 18;
constexpr size_t UID_HEX_SIZE = UID_SIZE * 2 + 1;
constexpr size_t ATTESTATION_TIMESTAMP_SIZE = 12;

// Object-policy access-rule bits from the SE05x applet policy header.
constexpr uint32_t POLICY_ALLOW_SIGN = 0x10000000;
constexpr uint32_t POLICY_ALLOW_VERIFY = 0x08000000;
constexpr uint32_t POLICY_ALLOW_KA = 0x04000000;
constexpr uint32_t POLICY_ALLOW_READ = 0x00200000;
constexpr uint32_t POLICY_ALLOW_WRITE = 0x00100000;
constexpr uint32_t POLICY_ALLOW_GEN = 0x00080000;
constexpr uint32_t POLICY_ALLOW_DELETE = 0x00040000;
constexpr uint32_t POLICY_REQUIRE_SM = 0x00020000;
constexpr uint32_t POLICY_ALLOW_ATTESTATION = 0x00008000;

class Client;

/**
 * Process-wide SE050 client, initialized by main.cpp after the I2C scan if an SE is
 * present and SELECT succeeds. Null if no chip is detected or applet SELECT failed.
 * Other firmware modules may read this pointer to reach the chip.
 * Lifetime: static object inside main.cpp's setup scope, stable for the duration of runtime.
 */
extern Client *client;

/**
 * Format an SE050 UID as lowercase hex. hexOut must be UID_HEX_SIZE bytes.
 */
void formatUIDHex(const uint8_t uid[UID_SIZE], char hexOut[UID_HEX_SIZE]);

/**
 * Host-side client for the NXP SE050 IoT Applet. Speaks T=1-over-I2C to the chip and
 * layers SCP03 secure messaging + applet-level crypto commands on top. Sufficient for
 * small APDUs (<= ~250 byte request / response, no chaining). Blocking, single-threaded.
 *
 * Usage:
 *   se050::Client se(&Wire1, 0x48);
 *   if (se.begin()) {
 *       uint8_t uid[se050::UID_SIZE];
 *       if (se.getUID(uid)) { ... }
 *   }
 */
/**
 * GP 2.3 Amd D SCP03 session state. Kept opaque to callers.
 *
 * We implement **security level 0x33**: full C-DECRYPTION + C-MAC + R-ENCRYPTION
 * + R-MAC. Required by SE050 applet v7.x for key-write operations (WriteECKey,
 * WriteRSAKey, etc.) -- these commands return SW=6A80 at lower security levels
 * despite accepting the bytes at the APDU layer.
 *
 * Command flow per GP 2.3 Amd D §6.2.4-6.2.7:
 *   1. Pad plaintext data: append 0x80 then 0x00 bytes to multiple of 16.
 *   2. IV = AES-ECB(S-ENC, counter_padded_to_16B_zero_prefix).
 *   3. Encrypt padded data with AES-CBC(S-ENC, IV).
 *   4. Build APDU with CLA |= 0x04 (MAC'd) and new Lc = encData + 8.
 *   5. C-MAC = first 8 bytes of CMAC(S-MAC, MCV || header || newLc || encData).
 *   6. MCV updated to full CMAC result.
 *
 * Response flow:
 *   1. Frame = [encRspData (0 or 16N bytes)] [R-MAC (8)] [SW (2)].
 *   2. R-MAC = CMAC(S-RMAC, MCV || encRspData || SW).
 *   3. Decrypt encRspData with AES-CBC using IV from same counter but high bit
 *      of counter block set (distinguishes command vs response IVs).
 *   4. Strip 0x80 padding.
 *   5. Encryption counter increments by 1 after each successful C-APDU.
 */
struct Scp03 {
    bool active;
    uint8_t sEnc[16];
    uint8_t sMac[16];
    uint8_t sRmac[16];
    uint8_t sDek[16];
    uint8_t mcv[16];     // C-MAC chaining value
    uint32_t encCounter; // 3-byte counter; increments after each C-APDU
};

struct ObjectInfo {
    bool exists;
    uint8_t type;
    bool isTransient;
    uint16_t size;
};

struct ObjectPolicy {
    uint32_t authObjectId;
    uint32_t keyPermissions;
    uint32_t commonPermissions;
};

struct AttestationResponse {
    uint8_t *data;
    size_t dataCapacity;
    size_t dataLen;
    uint8_t *attributes;
    size_t attributesCapacity;
    size_t attributesLen;
    uint8_t *chipId;
    size_t chipIdCapacity;
    size_t chipIdLen;
    uint8_t *timestamp;
    size_t timestampCapacity;
    size_t timestampLen;
    uint8_t *object;
    size_t objectCapacity;
    size_t objectLen;
    uint8_t *signature;
    size_t signatureCapacity;
    size_t signatureLen;
};

class Client
{
  public:
    Client(TwoWire *bus, uint8_t address);

    /**
     * Reset T=1 sequence state via S(RESYNC) then SELECT the IoT Applet.
     * Must be called once before transceive()/getUID().
     *
     * Optional outputs are filled from the SELECT-applet VersionInfo on success.
     */
    bool begin(uint8_t *majorOut = nullptr, uint8_t *minorOut = nullptr, uint8_t *patchOut = nullptr,
               uint16_t *configOut = nullptr, uint32_t timeout_ms = 500);

    /**
     * Send a raw APDU, receive the response. Handles NAD/PCB/LEN/CRC framing and
     * sequence number toggling. Does NOT apply SCP03 MAC wrapping even if a session
     * is open -- callers that need MAC protection must use sendSecure().
     * Returns response length in bytes on success (last two bytes are SW1/SW2),
     * or a negative error code.
     */
    int transceive(const uint8_t *apdu, size_t apduLen, uint8_t *rsp, size_t rspCapacity, uint32_t timeout_ms = 500);

    /**
     * Same as transceive() but wraps the APDU in SCP03 MAC mode if a session is
     * open, and falls back to plain transceive() otherwise. All write / create /
     * keygen / ECDH calls must go through this.
     */
    int sendSecure(const uint8_t *apdu, size_t apduLen, uint8_t *rsp, size_t rspCapacity, uint32_t timeout_ms = 500);

    /**
     * Read the 18-byte pre-provisioned chip UID (NXP object 0x7FFF0206) into uidOut.
     * Returns true if SW=9000 and exactly 18 bytes of UID were returned.
     * On success the UID is also cached inside the Client so later callers can
     * fetch it cheaply via getCachedUID() without another round-trip.
     */
    bool getUID(uint8_t uidOut[UID_SIZE], uint32_t timeout_ms = 500);

    /**
     * Non-blocking read of the UID cached by the most recent successful getUID()
     * call. Returns false if getUID() has not succeeded yet. Safe to call from
     * any context -- does not touch the I2C bus.
     *
     * Other modules can call this to surface the UID without issuing a redundant
     * APDU or racing against other SE050 traffic on the bus.
     */
    bool getCachedUID(uint8_t uidOut[UID_SIZE]) const;

    // --- Random --------------------------------------------------------------
    /**
     * Read `n` bytes of cryptographically strong randomness from the SE050's
     * certified TRNG. Does NOT require an SCP03 session.
     */
    bool getRandom(uint8_t *out, size_t n, uint32_t timeout_ms = 500);

    // --- SCP03 session -------------------------------------------------------
    /**
     * Open an SCP03 Platform session with the given 3x16-byte static keys (ENC,
     * MAC, DEK). For SE050E2 development use NXP factory defaults from AN12436
     * (OEF A921). Production deployments MUST rotate these per-device before deployment.
     *
     * Returns true if INITIALIZE UPDATE + EXTERNAL AUTHENTICATE both succeeded.
     */
    bool openPlatformScp03(const uint8_t encKey[16], const uint8_t macKey[16], const uint8_t dekKey[16],
                           uint32_t timeout_ms = 1000);

    bool isSecureSession() const { return scp.active; }

    // --- Applet-level operations (SCP03 required for write/create) ---------
    /**
     * Load the parameters for `curveId` (e.g. 0x41 = MontDH_25519, 0x40 = Ed25519).
     * Persistent across power cycles. Idempotent: returns true if the curve already
     * exists (SE050 returns SW=6A89 "data invalid" in that case; we treat that as OK).
     */
    bool createECCurve(uint8_t curveId, uint32_t timeout_ms = 1000);

    /**
     * Check whether a secure object exists. Does not mutate chip state.
     */
    bool objectExists(uint32_t objectId, bool *existsOut, uint32_t timeout_ms = 1000);

    /**
     * Read existence, secure-object type, transient flag, and object size.
     * Returns true for a valid APDU exchange even when the object does not exist;
     * in that case `infoOut->exists` is false and other fields are zero.
     */
    bool getObjectInfo(uint32_t objectId, ObjectInfo *infoOut, uint32_t timeout_ms = 1000);

    /**
     * Generate an EC keypair on-chip at `objectId` using `curveId`. The private
     * scalar remains inside the SE and is never extractable.
     */
    bool writeECKeyGen(uint32_t objectId, uint8_t curveId, uint32_t timeout_ms = 1000);

    /**
     * Generate an EC keypair with an object policy applied at creation time.
     * SE05x policies are immutable for an existing object; delete + recreate if
     * policy changes are required.
     */
    bool writeECKeyGenWithPolicy(uint32_t objectId, uint8_t curveId, const ObjectPolicy &policy, uint32_t timeout_ms = 1000);

    /**
     * Delete any secure object at `objectId`. Returns true on SW=9000 or if the
     * object didn't exist (SW=6A88 "reference data not found" treated as OK).
     * Used to guarantee a clean slate before WriteECKey -- SE050 rejects key
     * regeneration on an existing object with an explicit curveID.
     */
    bool deleteObject(uint32_t objectId, uint32_t timeout_ms = 1000);

    /**
     * Read the public half of EC key object `objectId`. For Curve25519 the output
     * is 32 bytes (X coordinate only). Writes the actual length to `*pubLenOut`.
     * Does not require SCP03.
     */
    bool readECPub(uint32_t objectId, uint8_t *pubOut, size_t pubCapacity, size_t *pubLenOut, uint32_t timeout_ms = 1000);

    /**
     * Read an object and request SE050 attestation over that read. `response`
     * contains caller-owned buffers; each `*Len` field is updated to the bytes
     * copied. Chip ID and signature buffers are required for a successful return;
     * null buffers for other fields simply skip those fields.
     */
    bool readObjectWithAttestation(uint32_t objectId, uint16_t offset, uint16_t length, uint32_t attestObjectId,
                                   uint8_t attestAlgo, const uint8_t *freshness, size_t freshnessLen,
                                   AttestationResponse *response, uint32_t timeout_ms = 1000);

    /**
     * Compute X25519 ECDH: `shared = ECDH(priv-at-objectId, peerPub)`.
     * `peerPub` is the 32-byte Curve25519 X coordinate. `shared` is 32 raw bytes.
     * Requires SCP03.
     */
    bool ecdhX25519(uint32_t privObjectId, const uint8_t peerPub[32], uint8_t shared[32], uint32_t timeout_ms = 1000);

    bool isReady() const { return ready; }

  private:
    TwoWire *bus;
    uint8_t address;
    uint8_t hostNS; // T=1 send-sequence bit (0 or 1), toggles after each I-block
    bool ready;
    Scp03 scp;

    // Populated by getUID() on SW=9000. Survives for the lifetime of the Client
    // so any downstream module can surface it without hitting the I2C bus again.
    // Never exposed when cachedUidValid==false.
    uint8_t cachedUid[UID_SIZE];
    bool cachedUidValid;

    // Returns true if the write was ACKed at the I2C layer.
    bool txFrame(uint8_t pcb, const uint8_t *inf, size_t infLen);

    // On success: returns number of INF bytes, writes PCB to *pcbOut and INF to infOut.
    // On failure: returns a negative error code; pcbOut/infOut undefined.
    int rxFrame(uint8_t *infOut, size_t infMax, uint8_t *pcbOut, uint32_t timeout_ms);

    // S(RESYNC request) -> S(RESYNC response). Resets hostNS to 0 on success.
    bool resync(uint32_t timeout_ms);
};

} // namespace se050

#endif // !MESHTASTIC_EXCLUDE_I2C
