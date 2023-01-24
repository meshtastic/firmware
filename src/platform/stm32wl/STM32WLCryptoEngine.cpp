#include "CryptoEngine.h"
#include "aes.hpp"
#include "configuration.h"

class STM32WLCryptoEngine : public CryptoEngine
{
  public:
    STM32WLCryptoEngine() {}

    ~STM32WLCryptoEngine() {}

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes) override
    {
        if (key.length > 0) {
            AES_ctx ctx;
            initNonce(fromNode, packetNum);
            AES_init_ctx_iv(&ctx, key.bytes, nonce);
            AES_CTR_xcrypt_buffer(&ctx, bytes, numBytes);
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes) override
    {
        // For CTR, the implementation is the same
        encrypt(fromNode, packetNum, numBytes, bytes);
    }

  private:
};

CryptoEngine *crypto = new STM32WLCryptoEngine();
