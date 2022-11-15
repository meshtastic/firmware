#include "configuration.h"
#include "CryptoEngine.h"
#include "aes.hpp"

class RP2040CryptoEngine : public CryptoEngine
{
  public:
    RP2040CryptoEngine() {}

    ~RP2040CryptoEngine() {}

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

CryptoEngine *crypto = new RP2040CryptoEngine();
