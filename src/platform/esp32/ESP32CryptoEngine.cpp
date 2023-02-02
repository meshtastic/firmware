#include "CryptoEngine.h"
#include "configuration.h"

#include "mbedtls/aes.h"

class ESP32CryptoEngine : public CryptoEngine
{

    mbedtls_aes_context aes;

  public:
    ESP32CryptoEngine() { mbedtls_aes_init(&aes); }

    ~ESP32CryptoEngine() { mbedtls_aes_free(&aes); }

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a _static_ buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(const CryptoKey &k) override
    {
        CryptoEngine::setKey(k);

        if (key.length != 0) {
            auto res = mbedtls_aes_setkey_enc(&aes, key.bytes, key.length * 8);
            assert(!res);
        }
    }

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
        if (key.length > 0) {
            LOG_DEBUG("ESP32 crypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t)packetId, numBytes);
            initNonce(fromNode, packetId);
            if (numBytes <= MAX_BLOCKSIZE) {
                static uint8_t scratch[MAX_BLOCKSIZE];
                uint8_t stream_block[16];
                size_t nc_off = 0;
                memcpy(scratch, bytes, numBytes);
                memset(scratch + numBytes, 0,
                       sizeof(scratch) - numBytes); // Fill rest of buffer with zero (in case cypher looks at it)

                auto res = mbedtls_aes_crypt_ctr(&aes, numBytes, &nc_off, nonce, stream_block, scratch, bytes);
                assert(!res);
            } else {
                LOG_ERROR("Packet too large for crypto engine: %d. noop encryption!\n", numBytes);
            }
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
        // For CTR, the implementation is the same
        encrypt(fromNode, packetId, numBytes, bytes);
    }

  private:
};

CryptoEngine *crypto = new ESP32CryptoEngine();
