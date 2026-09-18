#include "CryptoEngine.h"
#include "configuration.h"
#include <Adafruit_nRFCrypto.h>
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
};

CryptoEngine *crypto = new NRF52CryptoEngine();