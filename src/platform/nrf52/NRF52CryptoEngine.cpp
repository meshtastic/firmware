#include "CryptoEngine.h"
#include "aes-256/tiny-aes.h"
#include "configuration.h"
#include <Adafruit_nRFCrypto.h>
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
        if (key.length > 16) {
            LOG_DEBUG("Software encrypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t)packetId, numBytes);
            AES_ctx ctx;
            initNonce(fromNode, packetId);
            AES_init_ctx_iv(&ctx, key.bytes, nonce);
            AES_CTR_xcrypt_buffer(&ctx, bytes, numBytes);
        } else if (key.length > 0) {
            LOG_DEBUG("nRF52 encrypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t)packetId, numBytes);
            nRFCrypto.begin();
            nRFCrypto_AES ctx;
            uint8_t myLen = ctx.blockLen(numBytes);
            char encBuf[myLen] = {0};
            initNonce(fromNode, packetId);
            ctx.begin();
            ctx.Process((char *)bytes, numBytes, nonce, key.bytes, key.length, encBuf, ctx.encryptFlag, ctx.ctrMode);
            ctx.end();
            nRFCrypto.end();
            memcpy(bytes, encBuf, numBytes);
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
        // For CTR, the implementation is the same
        encrypt(fromNode, packetId, numBytes, bytes);
    }

  private:
};

CryptoEngine *crypto = new NRF52CryptoEngine();
