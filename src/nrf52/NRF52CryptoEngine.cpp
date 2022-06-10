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
        hexDump("before", bytes, numBytes, 16);
        if (key.length > 16) {
            DEBUG_MSG("Software encrypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t) packetId, numBytes);
            AES_ctx ctx;
            initNonce(fromNode, packetId);
            AES_init_ctx_iv(&ctx, key.bytes, nonce);
            AES_CTR_xcrypt_buffer(&ctx, bytes, numBytes);
        } else if (key.length > 0) {
            DEBUG_MSG("nRF52 encrypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t) packetId, numBytes);
            nRFCrypto.begin();
            nRFCrypto_AES ctx;
            uint8_t myLen = ctx.blockLen(numBytes);
            DEBUG_MSG("nRF52 encBuf myLen=%d!\n", myLen);
            char encBuf[myLen] = {0};
            initNonce(fromNode, packetId);
            ctx.begin();
            ctx.Process((char*)bytes, numBytes, nonce, key.bytes, key.length, encBuf, ctx.encryptFlag, ctx.ctrMode);
            ctx.end();
            nRFCrypto.end();
            memcpy(bytes, encBuf, numBytes);
        }
        hexDump("after", bytes, numBytes, 16);
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
        hexDump("before", bytes, numBytes, 16);
        if (key.length > 16) {
            DEBUG_MSG("Software decrypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t) packetId, numBytes);
            AES_ctx ctx;
            initNonce(fromNode, packetId);
            AES_init_ctx_iv(&ctx, key.bytes, nonce);
            AES_CTR_xcrypt_buffer(&ctx, bytes, numBytes);
        } else if (key.length > 0) {
            DEBUG_MSG("nRF52 decrypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t) packetId, numBytes);
            nRFCrypto.begin();
            nRFCrypto_AES ctx;
            uint8_t myLen = ctx.blockLen(numBytes);
            DEBUG_MSG("nRF52 decBuf myLen=%d!\n", myLen);
            char decBuf[myLen] = {0};
            initNonce(fromNode, packetId);
            ctx.begin();
            ctx.Process((char*)bytes, numBytes, nonce, key.bytes, key.length, decBuf, ctx.decryptFlag, ctx.ctrMode);
            ctx.end();
            nRFCrypto.end();
            memcpy(bytes, decBuf, numBytes);
        }
        hexDump("after", bytes, numBytes, 16);
    }

  private:
};

CryptoEngine *crypto = new NRF52CryptoEngine();
