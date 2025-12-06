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
     * Encrypt a packet
     *
     * @param bytes is updated in place
     *  TODO: return bool, and handle graciously when something fails
     */
    virtual void encryptAESCtr(CryptoKey _key, uint8_t *_nonce, size_t numBytes, uint8_t *bytes) override
    {
        if (_key.length > 0) {
            if (numBytes <= MAX_BLOCKSIZE) {
                mbedtls_aes_setkey_enc(&aes, _key.bytes, _key.length * 8);
                static uint8_t scratch[MAX_BLOCKSIZE];
                uint8_t stream_block[16];
                size_t nc_off = 0;
                memcpy(scratch, bytes, numBytes);
                memset(scratch + numBytes, 0,
                       sizeof(scratch) - numBytes); // Fill rest of buffer with zero (in case cypher looks at it)
                mbedtls_aes_crypt_ctr(&aes, numBytes, &nc_off, _nonce, stream_block, scratch, bytes);
            } else {
                LOG_ERROR("Packet too large for crypto engine: %d. noop encryption!", numBytes);
            }
        }
    }
};

CryptoEngine *crypto = new ESP32CryptoEngine();