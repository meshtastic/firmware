#include "CryptoEngine.h"
#include "configuration.h"
#include <Adafruit_nRFCrypto.h>
#include <nrf_cc310/include/crys_ec_mont_edw_error.h>
class NRF52CryptoEngine : public CryptoEngine
{
  public:
    NRF52CryptoEngine() {}

    ~NRF52CryptoEngine() {}

    virtual void encryptAESCtr(CryptoKey _key, uint8_t *_nonce, size_t numBytes, uint8_t *bytes) override
    {
        if (_key.length > 16) {
            // CryptoCell only accelerates AES-128; AES-256 uses the shared software CTR.
            CryptoEngine::encryptAESCtr(_key, _nonce, numBytes, bytes);
        } else if (_key.length > 0) {
            nRFCrypto.begin();
            nRFCrypto_AES ctx;
            // Must not be uint8_t: blockLen() rounds up to the AES block size, so numBytes in
            // 241..256 yields 256, which truncates to 0 and leaves Process() writing numBytes
            // attacker-controlled bytes onto a zero-length stack buffer.
            size_t myLen = ctx.blockLen(numBytes);
            char encBuf[myLen] = {0};
            ctx.begin();
            ctx.Process((char *)bytes, numBytes, _nonce, _key.bytes, _key.length, encBuf, ctx.encryptFlag, ctx.ctrMode);
            ctx.end();
            nRFCrypto.end();
            memcpy(bytes, encBuf, numBytes);
        }
    }

#if !(MESHTASTIC_EXCLUDE_PKI)
  protected:
    // CC310 X25519, about 5x faster than software. The CC310 clamps and eval doesn't, so only
    // clamped scalars (every key dh1 or regeneratePublicKey leaves) go to the hardware; others keep the software result.
    bool x25519(uint8_t *out, const uint8_t *scalar, const uint8_t *point) override
    {
        if (!isClamped(scalar))
            return CryptoEngine::x25519(out, scalar, point);
        if (point && !isCanonical(point))
            return false; // matches Curve25519::eval
        if (!nRFCrypto.begin())
            return CryptoEngine::x25519(out, scalar, point);
        bool ok = point ? cc310X25519.agree(out, scalar, point) : cc310X25519.publicKey(out, scalar);
        uint32_t err = cc310X25519.lastError();
        nRFCrypto.end();
        if (ok)
            return true;
        if (err == CRYS_OK) {
            // agree() refused an all-zero result; report it as weak, as the software would be.
            memset(out, 0, 32);
            return true;
        }
        LOG_WARN("CC310 X25519 error 0x%08x, using software", (unsigned int)err);
        return CryptoEngine::x25519(out, scalar, point);
    }

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    // CC310 Ed25519 verify, about 10x faster than software. Only a definite accept or reject from the
    // CC310 is final; any other error falls back to software so a hardware fault can't drop valid packets.
    bool ed25519Verify(const uint8_t *signature, const uint8_t *edPubKey, const uint8_t *msg, size_t msgLen) override
    {
        if (!nRFCrypto.begin())
            return CryptoEngine::ed25519Verify(signature, edPubKey, msg, msgLen);
        bool ok = cc310Ed25519.verify(signature, msg, msgLen, edPubKey);
        uint32_t err = cc310Ed25519.lastError();
        nRFCrypto.end();
        if (ok)
            return true;
        if (err == CRYS_ECEDW_SIGN_VERIFY_FAILED_ERROR)
            return false;
        LOG_DEBUG("CC310 Ed25519 verify error 0x%08x, using software", (unsigned int)err);
        return CryptoEngine::ed25519Verify(signature, edPubKey, msg, msgLen);
    }

    // CC310 R = r * B for XEdDSA signing: about 7x faster than software, and the signature is identical.
    // A known answer gates it on first use; a mismatch or any error leaves R to the software.
    bool ed25519BaseMul(uint8_t *point, const uint8_t *scalar) override
    {
        if (baseMulSelfTest == SelfTest::Untested) {
            // RFC 8032 7.1 TEST 1: its secret scalar a (mod q) times B is its public key.
            static const uint8_t katScalar[32] = {0x7c, 0x2c, 0xac, 0x12, 0xe6, 0x9b, 0xe9, 0x6a, 0xe9, 0x06, 0x50,
                                                  0x65, 0x46, 0x23, 0x85, 0xe8, 0xfc, 0xff, 0x27, 0x68, 0xd9, 0x80,
                                                  0xc0, 0xa3, 0xa5, 0x20, 0xf0, 0x06, 0x90, 0x4d, 0xe9, 0x0f};
            static const uint8_t katPoint[32] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                                                 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                                                 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
            uint8_t out[32];
            bool ok = cc310BaseMul(out, katScalar) && memcmp(out, katPoint, sizeof(out)) == 0;
            baseMulSelfTest = ok ? SelfTest::Passed : SelfTest::Failed;
            if (!ok)
                LOG_ERROR("CC310 Ed25519 base point self-test failed, signing in software");
        }
        return baseMulSelfTest == SelfTest::Passed && cc310BaseMul(point, scalar);
    }
#endif

  private:
    static bool isClamped(const uint8_t *k) { return (k[0] & 7) == 0 && (k[31] & 0xC0) == 0x40; }

    // True if u (top bit ignored, as RFC 7748 says) is below p = 2^255 - 19.
    static bool isCanonical(const uint8_t *u)
    {
        if ((u[31] & 0x7F) != 0x7F || u[0] < 0xED)
            return true;
        for (int i = 1; i < 31; i++)
            if (u[i] != 0xFF)
                return true;
        return false;
    }

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    bool cc310BaseMul(uint8_t *point, const uint8_t *scalar)
    {
        if (!nRFCrypto.begin())
            return false;
        bool ok = cc310Ed25519.scalarMultBase(point, scalar);
        uint32_t err = cc310Ed25519.lastError();
        nRFCrypto.end();
        if (!ok)
            LOG_WARN("CC310 Ed25519 base point error 0x%08x, using software", (unsigned int)err);
        return ok;
    }
#endif

    nRFCrypto_X25519 cc310X25519;
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    nRFCrypto_Ed25519 cc310Ed25519;
    enum class SelfTest : uint8_t { Untested, Passed, Failed };
    SelfTest baseMulSelfTest = SelfTest::Untested;
#endif
#endif
};

CryptoEngine *crypto = new NRF52CryptoEngine();
