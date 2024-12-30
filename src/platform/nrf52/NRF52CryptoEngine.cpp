#include "CryptoEngine.h"
#include "aes-256/tiny-aes.h"
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
            AES_ctx ctx;
            AES_init_ctx_iv(&ctx, _key.bytes, _nonce);
            AES_CTR_xcrypt_buffer(&ctx, bytes, numBytes);
        } else if (_key.length > 0) {
            nRFCrypto.begin();
            nRFCrypto_AES ctx;
            uint8_t myLen = ctx.blockLen(numBytes);
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