#include "AES.h"
#include "CTR.h"
#include "CryptoEngine.h"
#include "configuration.h"

class RP2040CryptoEngine : public CryptoEngine
{

    CTRCommon *ctr = NULL;

  public:
    RP2040CryptoEngine() {}

    ~RP2040CryptoEngine() {}

    virtual void setKey(const CryptoKey &k) override
    {
        CryptoEngine::setKey(k);
        LOG_DEBUG("Installing AES%d key!\n", key.length * 8);
        if (ctr) {
            delete ctr;
            ctr = NULL;
        }
        if (key.length != 0) {
            if (key.length == 16)
                ctr = new CTR<AES128>();
            else
                ctr = new CTR<AES256>();

            ctr->setKey(key.bytes, key.length);
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
            initNonce(fromNode, packetId);
            if (numBytes <= MAX_BLOCKSIZE) {
                static uint8_t scratch[MAX_BLOCKSIZE];
                memcpy(scratch, bytes, numBytes);
                memset(scratch + numBytes, 0,
                       sizeof(scratch) - numBytes); // Fill rest of buffer with zero (in case cypher looks at it)

                ctr->setIV(nonce, sizeof(nonce));
                ctr->setCounterSize(4);
                ctr->encrypt(bytes, scratch, numBytes);
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

CryptoEngine *crypto = new RP2040CryptoEngine();
