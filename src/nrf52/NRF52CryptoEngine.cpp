#include "configuration.h"
#include "CryptoEngine.h"
#include "ocrypto_aes_ctr.h"

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
    virtual void encrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
    {
        // DEBUG_MSG("NRF52 encrypt!\n");

        if (key.length > 0) {
            ocrypto_aes_ctr_ctx ctx;

            initNonce(fromNode, packetNum);
            ocrypto_aes_ctr_init(&ctx, key.bytes, key.length, nonce);

            ocrypto_aes_ctr_encrypt(&ctx, bytes, bytes, numBytes);
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
    {
        // DEBUG_MSG("NRF52 decrypt!\n");

        if (key.length > 0) {
            ocrypto_aes_ctr_ctx ctx;

            initNonce(fromNode, packetNum);
            ocrypto_aes_ctr_init(&ctx, key.bytes, key.length, nonce);

            ocrypto_aes_ctr_decrypt(&ctx, bytes, bytes, numBytes);
        }
    }

  private:
};

CryptoEngine *crypto = new NRF52CryptoEngine();
