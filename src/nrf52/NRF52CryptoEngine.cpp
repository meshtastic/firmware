#include "configuration.h"
#include "CryptoEngine.h"
#include "ocrypto_aes_ctr.h"
// #include <Adafruit_nRFCrypto.h>

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

        if (key.length > 0) {
            ocrypto_aes_ctr_ctx ctx;

            initNonce(fromNode, packetId);
            ocrypto_aes_ctr_init(&ctx, key.bytes, key.length, nonce);

            ocrypto_aes_ctr_encrypt(&ctx, bytes, bytes, numBytes);
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
//        DEBUG_MSG("NRF52 decrypt!\n");

        if (key.length > 0) {
            ocrypto_aes_ctr_ctx ctx;

            initNonce(fromNode, packetId);
            ocrypto_aes_ctr_init(&ctx, key.bytes, key.length, nonce);

            ocrypto_aes_ctr_decrypt(&ctx, bytes, bytes, numBytes);
        }
    }

  private:
};

    // /**
    //  * Encrypt a packet
    //  *
    //  * @param bytes is updated in place
    //  */
    // virtual void encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    // {
    //     DEBUG_MSG("NRF52 encrypt!\n");

    //     if (key.length > 0) {
    //         nRFCrypto_AES ctx;
    //         uint8_t myLen = ctx.blockLen(numBytes);
    //         char encBuf[myLen] = {0};
    //         memcpy(encBuf, bytes, numBytes);
    //         initNonce(fromNode, packetId);
    //         nRFCrypto.begin();
    //         ctx.begin();
    //         ctx.Process(encBuf, numBytes, nonce, key.bytes, key.length, (char*)bytes, ctx.encryptFlag, ctx.ctrMode);
    //         ctx.end();
    //         nRFCrypto.end();
    //     }
    // }

    // virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    // {
    //     DEBUG_MSG("NRF52 decrypt!\n");

    //     if (key.length > 0) {
    //         nRFCrypto_AES ctx;
    //         uint8_t myLen = ctx.blockLen(numBytes);
    //         char decBuf[myLen] = {0};
    //         memcpy(decBuf, bytes, numBytes);
    //         initNonce(fromNode, packetId);
    //         nRFCrypto.begin();
    //         ctx.begin();
    //         ctx.Process(decBuf, numBytes, nonce, key.bytes, key.length, (char*)bytes, ctx.decryptFlag, ctx.ctrMode);
    //         ctx.end();
    //         nRFCrypto.end();
    //     }
    // }

//   private:
// };

CryptoEngine *crypto = new NRF52CryptoEngine();
