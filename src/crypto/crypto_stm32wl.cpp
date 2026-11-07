#include "crypto_backends.h"

#ifdef CRYPTO_HAS_STM32_AES
#include "concurrency/LockGuard.h"
#ifdef ARCH_STM32WL
#include <stm32wlxx.h>
#endif
#include <Crypto.h>
#include <string.h>

static constexpr size_t AES_BLOCK = 16;
static constexpr size_t CCM_L = 2;
// A block takes hundreds of clocks at most, so this only trips on a hung peripheral.
static constexpr uint32_t CCF_POLLS = 10000;
static constexpr uint32_t CR_BYTES = AES_CR_DATATYPE_1;

// Function-local static: at file scope its constructor would keep it out of linker GC.
static concurrency::Lock &aesLock()
{
    static concurrency::Lock lock;
    return lock;
}

// The reset also clears the key registers, so a key doesn't outlive its call.
static void aesReset()
{
    RCC->AHB3RSTR |= RCC_AHB3RSTR_AESRST;
    RCC->AHB3RSTR &= ~RCC_AHB3RSTR_AESRST;
}

static void aesPowerUp()
{
    RCC->AHB3ENR |= RCC_AHB3ENR_AESEN;
    (void)RCC->AHB3ENR;
    aesReset();
}

static void aesPowerDown()
{
    aesReset();
    RCC->AHB3ENR &= ~RCC_AHB3ENR_AESEN;
}

static uint32_t loadBe32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void setKey(const uint8_t *key, size_t key_len)
{
    if (key_len == 32) {
        AES->KEYR7 = loadBe32(key);
        AES->KEYR6 = loadBe32(key + 4);
        AES->KEYR5 = loadBe32(key + 8);
        AES->KEYR4 = loadBe32(key + 12);
        key += 16;
    }
    AES->KEYR3 = loadBe32(key);
    AES->KEYR2 = loadBe32(key + 4);
    AES->KEYR1 = loadBe32(key + 8);
    AES->KEYR0 = loadBe32(key + 12);
}

static void setIv(const uint8_t iv[AES_BLOCK])
{
    AES->IVR3 = loadBe32(iv);
    AES->IVR2 = loadBe32(iv + 4);
    AES->IVR1 = loadBe32(iv + 8);
    AES->IVR0 = loadBe32(iv + 12);
}

static void setCrField(uint32_t mask, uint32_t value)
{
    AES->CR = (AES->CR & ~mask) | value;
}

static bool waitCcf()
{
    for (uint32_t i = 0; i < CCF_POLLS; i++) {
        if (AES->SR & AES_SR_CCF)
            return true;
    }
    return false;
}

static void clearCcf()
{
    AES->CR |= AES_CR_CCFC;
}

// With byte data type the peripheral swaps each word, so DINR and DOUTR take words in memory order.
static void writeBlock(const uint8_t *block)
{
    for (size_t i = 0; i < AES_BLOCK; i += 4) {
        uint32_t word;
        memcpy(&word, block + i, 4);
        AES->DINR = word;
    }
}

static void readBlock(uint8_t *block)
{
    for (size_t i = 0; i < AES_BLOCK; i += 4) {
        const uint32_t word = AES->DOUTR;
        memcpy(block + i, &word, 4);
    }
}

// Goes through a local block so that flash, unaligned and in-place buffers all work.
static bool runBlocks(const uint8_t *in, size_t len, uint8_t *out, bool ccmDecrypt)
{
    uint8_t block[AES_BLOCK];
    bool ok = true;
    for (size_t off = 0; ok && off < len; off += AES_BLOCK) {
        const size_t n = len - off < AES_BLOCK ? len - off : AES_BLOCK;
        memset(block, 0, sizeof(block));
        memcpy(block, in + off, n);
        // Decrypting, the padding must become zero plaintext for the MAC. Encrypting, zero input already is.
        if (ccmDecrypt && n < AES_BLOCK)
            setCrField(AES_CR_NPBLB, (uint32_t)(AES_BLOCK - n) << AES_CR_NPBLB_Pos);
        writeBlock(block);
        ok = waitCcf();
        if (ok) {
            clearCcf();
            readBlock(block);
            memcpy(out + off, block, n);
        }
    }
    clean(block);
    return ok;
}

bool crypto_stm32wl_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                            uint8_t *out)
{
    concurrency::LockGuard guard(&aesLock());
    aesPowerUp();
    AES->CR = CR_BYTES | AES_CR_CHMOD_1 | (key_len == 32 ? AES_CR_KEYSIZE : 0);
    setKey(key, key_len);
    setIv(counter);
    AES->CR |= AES_CR_EN;
    const bool ok = runBlocks(in, len, out, false);
    aesPowerDown();
    if (!ok) {
        LOG_ERROR("AES-CTR peripheral timeout");
        memset(out, 0, len);
    }
    return ok;
}

static bool ccmHeader(const uint8_t *aad, size_t aad_len)
{
    uint8_t head[2 * AES_BLOCK] = {0};
    head[1] = aad_len;
    memcpy(&head[2], aad, aad_len);
    for (size_t off = 0; off < aad_len + 2; off += AES_BLOCK) {
        writeBlock(head + off);
        if (!waitCcf())
            return false;
        clearCcf();
    }
    return true;
}

// Order per CRYP_AESCCM_Process and HAL_CRYPEx_AESCCM_GenerateAuthTAG: EN clears itself after the init phase, not after later
// ones.
static bool ccmRun(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                   const uint8_t *in, size_t len, uint8_t *out, size_t tag_len, bool decrypt, uint8_t fullTag[16])
{
    uint8_t b0[AES_BLOCK];
    b0[0] = (aad_len ? 0x40 : 0) | (((tag_len - 2) / 2) << 3) | (CCM_L - 1);
    memcpy(&b0[1], nonce, 15 - CCM_L);
    b0[14] = len >> 8;
    b0[15] = len & 0xff;

    AES->CR = CR_BYTES | AES_CR_CHMOD_2 | (key_len == 32 ? AES_CR_KEYSIZE : 0) | (decrypt ? AES_CR_MODE_1 : 0);
    setKey(key, key_len);
    setIv(b0);
    AES->CR |= AES_CR_EN;
    if (!waitCcf())
        return false;
    clearCcf();

    setCrField(AES_CR_GCMPH, AES_CR_GCMPH_0);
    AES->CR |= AES_CR_EN;
    if (aad_len && !ccmHeader(aad, aad_len))
        return false;

    setCrField(AES_CR_GCMPH, AES_CR_GCMPH_1);
    if (!runBlocks(in, len, out, decrypt))
        return false;

    setCrField(AES_CR_GCMPH | AES_CR_MODE, AES_CR_GCMPH);
    if (!waitCcf())
        return false;
    readBlock(fullTag);
    clearCcf();
    return true;
}

bool crypto_stm32wl_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                 const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    concurrency::LockGuard guard(&aesLock());
    uint8_t fullTag[AES_BLOCK];
    aesPowerUp();
    const bool ok = ccmRun(key, key_len, nonce, aad, aad_len, in, len, out, tag_len, false, fullTag);
    aesPowerDown();
    if (ok) {
        memcpy(tag, fullTag, tag_len);
    } else {
        LOG_ERROR("AES-CCM peripheral timeout");
        if (len)
            memset(out, 0, len);
        memset(tag, 0, tag_len);
    }
    clean(fullTag);
    return ok;
}

bool crypto_stm32wl_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                 const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out)
{
    concurrency::LockGuard guard(&aesLock());
    uint8_t fullTag[AES_BLOCK];
    aesPowerUp();
    bool ok = ccmRun(key, key_len, nonce, aad, aad_len, in, len, out, tag_len, true, fullTag);
    aesPowerDown();
    if (ok) {
        uint8_t diff = 0;
        for (size_t i = 0; i < tag_len; i++)
            diff |= fullTag[i] ^ tag[i];
        ok = diff == 0;
    } else {
        LOG_ERROR("AES-CCM peripheral timeout");
    }
    if (!ok && len)
        memset(out, 0, len);
    clean(fullTag);
    return ok;
}
#endif
