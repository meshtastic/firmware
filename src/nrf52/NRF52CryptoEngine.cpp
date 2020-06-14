
#include "CryptoEngine.h"
#include "configuration.h"
#include "ocrypto_aes_ctr.h"

class NRF52CryptoEngine : public CryptoEngine
{

    /// How many bytes in our key
    uint8_t keySize = 0;
    const uint8_t *keyBytes;

  public:
    NRF52CryptoEngine() {}

    ~NRF52CryptoEngine() {}

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a _static_ buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(size_t numBytes, uint8_t *bytes)
    {
        keySize = numBytes;
        keyBytes = bytes;
    }

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
    {
        // DEBUG_MSG("NRF52 encrypt!\n");

        if (keySize != 0) {
            ocrypto_aes_ctr_ctx ctx;

            initNonce(fromNode, packetNum);
            ocrypto_aes_ctr_init(&ctx, keyBytes, keySize, nonce);

            ocrypto_aes_ctr_encrypt(&ctx, bytes, bytes, numBytes);
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
    {
        // DEBUG_MSG("NRF52 decrypt!\n");

        if (keySize != 0) {
            ocrypto_aes_ctr_ctx ctx;

            initNonce(fromNode, packetNum);
            ocrypto_aes_ctr_init(&ctx, keyBytes, keySize, nonce);

            ocrypto_aes_ctr_decrypt(&ctx, bytes, bytes, numBytes);
        }
    }

  private:
};

CryptoEngine *crypto = new NRF52CryptoEngine();
