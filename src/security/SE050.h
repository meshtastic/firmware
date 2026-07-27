#pragma once

// NXP SE050 secure element over T=1oI2C (UM11225).
//
// Covers the whole path this driver needs: block framing (NAD PCB LEN INF CRC)
// and the interface reset that returns the ATR, APDU exchange over that
// transport, a PlatformSCP03 secure channel on top of it (the SE050 refuses
// key agreement outside an authenticated channel), and an X25519 identity that
// either lives in the chip from the start (identityEnsure) or is imported to
// mirror a key the caller already holds (identityImport) for ECDH
// (identityEcdh). Validated on silicon against an SE050E2, applet 7.2.0.

#include "configuration.h"

#if defined(HAS_SE050)

#include <Wire.h>
#include <stddef.h>
#include <stdint.h>

class SE050
{
  public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x48;

    // Wire must already be begun by the caller - the I2C bus is shared, so this
    // class never configures or owns it.
    SE050(TwoWire &bus, uint8_t address = DEFAULT_ADDRESS) : bus(bus), address(address) {}

    // Interface reset (S-block). The SE050 answers with its ATR, which also
    // resynchronises the block layer. Returns true and fills atrOut/atrLen on
    // success. Safe to call repeatedly.
    bool reset(uint8_t *atrOut = nullptr, size_t atrCap = 0, size_t *atrLen = nullptr);

    // Reset, then SELECT the IoT applet. Every APDU exchange needs the applet
    // selected first. Returns true on SW=9000.
    bool open();

    // Sends one APDU wrapped in an I-block and returns the R-APDU (response INF,
    // trailing SW included). Handles WTX. Returns the R-APDU length, or -1.
    int transceive(const uint8_t *apdu, size_t apduLen, uint8_t *resp, size_t respCap);

    // Trailing status word of an R-APDU.
    static uint16_t statusWord(const uint8_t *resp, int len);

    // Opens a PlatformSCP03 secure channel on top of an already-open applet:
    // INITIALIZE UPDATE, derive the session keys, verify the card cryptogram,
    // then EXTERNAL AUTHENTICATE. The SE050 refuses key agreement outside an
    // authenticated channel, so this is a prerequisite, not a hardening step.
    //
    // Verifying the card cryptogram also authenticates the chip to us: it can
    // only be reproduced with the right static keys and KDF.
    bool openSecureChannel();

    // Ensures this node has an X25519 identity inside the SE050, generating it on
    // first use and reusing it afterwards. The private half is created in the chip
    // and never leaves it. Returns the public key, little-endian as the rest of
    // Meshtastic expects. Requires an open secure channel.
    bool identityEnsure(uint8_t publicKey[32]);

    // Mirrors an existing X25519 private key into the chip so the SE050 can run the
    // key agreement for an identity Meshtastic already owns. Idempotent: if the
    // object already holds this key nothing is written, so the NVM write happens
    // once in the life of the node rather than once per boot.
    //
    // Returns false and writes nothing if the object exists holding a different
    // key - rotating an identity means deleting the object first, which is a
    // deliberate decision and not something to do implicitly. Pass replaceStale
    // to make that decision explicitly: the old object is deleted and the
    // current node key mirrored in its place.
    bool identityImport(const uint8_t privateKey[32], uint8_t publicKeyOut[32], bool replaceStale = false);

    // One key agreement against the on-chip identity. Peer key and output are
    // little-endian; the SE050 works big-endian, so both are reversed here.
    bool identityEcdh(const uint8_t peerPublic[32], uint8_t shared[32]);

    // Bring-up check for all four layers.
    bool probe();

  private:
    // Writes one block and reads the answer. Returns the total framed length
    // (3 + LEN + 2), or 0 if nothing valid came back.
    size_t xfer(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxCap);

    // True while xfer is parked waiting for the chip - the only point in a
    // transaction where control can leave the driver. Anything that would start
    // fresh work on the chip from there is a re-entrant call and gets refused:
    // it would advance the SCP03 counter out from under the transaction in
    // flight and overwrite the buffers it is still using.
    bool waiting = false;
    bool reentered(const char *what);

    bool selectApplet();

    // Curve, authenticator and UserID session - the idempotent preamble both
    // identity paths need before they can touch a key object.
    bool identitySession();

    static uint16_t crc(const uint8_t *data, size_t len);

    // SCP03 session state. mcv is the MAC chaining value: zero until the first
    // C-MAC, then the full CMAC of the previous command.
    struct Scp03 {
        uint8_t senc[16];  // command data encryption
        uint8_t smac[16];  // command MAC
        uint8_t srmac[16]; // response MAC
        uint8_t mcv[16];
        uint32_t counter; // command counter, drives the encryption ICV
        bool open;
    };

    // Runs one APDU inside the secure channel: encrypt and MAC the command, then
    // verify and decrypt the response. Returns the plaintext length (SW stripped).
    int secureApdu(const uint8_t header[4], const uint8_t *data, int dataLen, bool expectResponse, uint8_t *resp, int respCap,
                   uint16_t *sw);

    // Same, but nested inside a UserID session (ProcessSessionCmd). Key agreement
    // needs the object bound to a session authenticator; the secure channel alone
    // is only transport and is not enough.
    int sessionApdu(const uint8_t header[4], const uint8_t *data, int dataLen, bool expectResponse, uint8_t *resp, int respCap,
                    uint16_t *sw);

    void encryptionIcv(bool response, uint8_t icv[16]);
    static void cbc(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out, bool encrypt);
    static const uint8_t *tlv1(const uint8_t *resp, int len, int *valueLen);
    static void reverse(const uint8_t *in, uint8_t *out, size_t len);

    static void cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t out[16]);
    static void kdf(const uint8_t key[16], uint8_t constant, uint16_t bits, const uint8_t context[16], uint8_t out[16]);
    void sessionKeys(const uint8_t context[16]);
    void cryptogram(uint8_t constant, const uint8_t context[16], uint8_t out[8]);
    void chainedCmac(const uint8_t *cmd, size_t len, uint8_t mac[8]);
    bool initializeUpdate(const uint8_t hostChallenge[8], uint8_t cardChallenge[8], uint8_t cardCryptogram[8]);

    // Transaction buffers, held in the object rather than on the stack.
    //
    // One key agreement nests identityEcdh -> sessionApdu -> secureApdu ->
    // transceive -> xfer, and as locals these came to roughly 2.5 KB in a
    // single call chain entered from deep inside packet handling. That is what
    // rebooted the board on every real PKI message while the same code passed
    // a self-test from setup(), where the frame underneath it is shallow.
    // Moving them here costs ~2.2 KB of a RAM budget that is 18% used and takes
    // the chain down to a few hundred bytes of stack.
    //
    // Sharing one set of buffers across the nesting is safe because the levels
    // use different ones and reentered() refuses any overlapping transaction.
    uint8_t txFrame[288];  // block written to the chip
    uint8_t rxFrame[288];  // block read back
    uint8_t apduOut[300];  // wrapped command built by secureApdu
    uint8_t apduIn[300];   // its response
    uint8_t encBuf[256];   // C-DATA after encryption
    uint8_t padBuf[256];   // C-DATA before it, padded
    uint8_t macBuf[274];   // MCV || response || SW, the R-MAC input
    uint8_t plainBuf[256]; // decrypted response
    uint8_t sessionBuf[288];

    TwoWire &bus;
    uint8_t address;
    uint8_t seq = 0; // host N(S), toggled per I-block, reset by the interface reset
    Scp03 scp = {};
    uint8_t sessionId[8] = {};
    bool identityReady = false;
    // A UserID session, once opened, stays open for the run. Tracked so the second
    // caller reuses it instead of asking the chip to open another one.
    bool sessionActive = false;
    // Which key object identityEcdh works against: the chip-generated identity or
    // the mirrored node key, depending on which path prepared it.
    uint32_t activeKeyObj = 0;
};

// The instance the boot probe left behind, or null if this board has no SE050 or
// the chip did not answer. Anything wanting to use the secure element after boot
// goes through this rather than opening its own channel.
extern SE050 *se050;

#if defined(HAS_CUSTOM_CRYPTO_ENGINE)
// Drives one key agreement through the live CryptoEngine and checks the chip
// against the software implementation. Called once NodeDB has loaded the node's
// private key, because that is what the mirror copies into the SE050. Doing this
// on the real object, rather than on a scratch instance, is the point: it proves
// the path packets actually take.
void se050CryptoSelfTest();
#endif

#endif // HAS_SE050
