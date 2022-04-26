#include "configuration.h"
#include "CryptoEngine.h"
#include <Adafruit_nRFCrypto.h>
#include "aes-256/tiny-aes.h"
class NRF52CryptoEngine : public CryptoEngine
{
  public:
    NRF52CryptoEngine() {}

    ~NRF52CryptoEngine() {}

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
//        DEBUG_MSG("NRF52 encrypt!\n");

        if (key.length > 16) {
            AES_ctx ctx;
            initNonce(fromNode, packetId);
            AES_init_ctx_iv(&ctx, key.bytes, nonce);
            AES_CTR_xcrypt_buffer(&ctx, bytes, numBytes);
        } else if (key.length > 0) {
            nRFCrypto.begin();
            nRFCrypto_AES ctx;
            uint8_t myLen = ctx.blockLen(numBytes);
            char encBuf[myLen] = {0};
            memcpy(encBuf, bytes, numBytes);
            initNonce(fromNode, packetId);
            ctx.begin();
            ctx.Process(encBuf, numBytes, nonce, key.bytes, key.length, (char*)bytes, ctx.encryptFlag, ctx.ctrMode);
            ctx.end();
            nRFCrypto.end();
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
//        DEBUG_MSG("NRF52 decrypt!\n");

        if (key.length > 16) {
            AES_ctx ctx;
            initNonce(fromNode, packetId);
            AES_init_ctx_iv(&ctx, key.bytes, nonce);
            AES_CTR_xcrypt_buffer(&ctx, bytes, numBytes);
        } else if (key.length > 0) {
            nRFCrypto.begin();
            nRFCrypto_AES ctx;
            uint8_t myLen = ctx.blockLen(numBytes);
            char decBuf[myLen] = {0};
            memcpy(decBuf, bytes, numBytes);
            initNonce(fromNode, packetId);
            ctx.begin();
            ctx.Process(decBuf, numBytes, nonce, key.bytes, key.length, (char*)bytes, ctx.decryptFlag, ctx.ctrMode);
            ctx.end();
            nRFCrypto.end();
        }
    }

  private:
};

CryptoEngine *crypto = new NRF52CryptoEngine();
