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
    // CC310 X25519 (~5x faster, no ~3 KB eval stack frame). The CC310 clamps and eval doesn't, so only
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

    nRFCrypto_X25519 cc310X25519;
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    nRFCrypto_Ed25519 cc310Ed25519;
#endif
#endif
};

CryptoEngine *crypto = new NRF52CryptoEngine();
