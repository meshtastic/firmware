#include "CryptoEngine.h"
#include "configuration.h"

#include "crypto/includes.h"

#include "crypto/common.h"

// #include "esp_system.h"

#include "crypto/aes.h"
#include "crypto/aes_wrap.h"
#include "mbedtls/aes.h"



class ESP32CryptoEngine : public CryptoEngine
{

    mbedtls_aes_context aes;

    /// How many bytes in our key
    uint8_t keySize = 0;

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
    virtual void setKey(size_t numBytes, uint8_t *bytes)
    {
        keySize = numBytes;
        DEBUG_MSG("Installing AES%d key!\n", numBytes * 8);
        if (numBytes != 0) {
            auto res = mbedtls_aes_setkey_enc(&aes, bytes, numBytes * 8);
            assert(!res);
        }
    }

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
    {
        if (keySize != 0) {
            uint8_t stream_block[16];
            static uint8_t scratch[MAX_BLOCKSIZE];
            size_t nc_off = 0;

            // DEBUG_MSG("ESP32 encrypt!\n");
            initNonce(fromNode, packetNum);
            assert(numBytes <= MAX_BLOCKSIZE);
            memcpy(scratch, bytes, numBytes);
            memset(scratch + numBytes, 0,
                   sizeof(scratch) - numBytes); // Fill rest of buffer with zero (in case cypher looks at it)

            auto res = mbedtls_aes_crypt_ctr(&aes, numBytes, &nc_off, nonce, stream_block, scratch, bytes);
            assert(!res);
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes)
    {
        // DEBUG_MSG("ESP32 decrypt!\n");

        // For CTR, the implementation is the same
        encrypt(fromNode, packetNum, numBytes, bytes);
    }

  private:
};

CryptoEngine *crypto = new ESP32CryptoEngine();
