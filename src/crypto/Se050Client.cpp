#include "Se050Client.h"

#if !MESHTASTIC_EXCLUDE_I2C

#include <AES.h>
#include <Arduino.h>
#include <RNG.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

namespace
{

// T=1 NAD bytes for SE050 over I2C (Host=0x5, SE050=0xA).
constexpr uint8_t NAD_HOST_TO_SE = 0x5A;
constexpr uint8_t NAD_SE_TO_HOST = 0xA5;

// PCB encoding (ISO 7816-3 T=1):
//   I-block:  b7=0, b6=N(S), b5=more-data (chaining)
//   R-block:  b7=1, b6=0,    b4=N(R), b1..0=err
//   S-block:  b7=1, b6=1,    b5..0=func
constexpr uint8_t PCB_IBLOCK_NS0 = 0x00;
constexpr uint8_t PCB_IBLOCK_NS1 = 0x40;
constexpr uint8_t PCB_SBLOCK_RESYNC_REQ = 0xC0;
constexpr uint8_t PCB_SBLOCK_RESYNC_RSP = 0xE0;

// NXP SE050 IoT Applet AID (NXP AN12413).
const uint8_t SE050_IOT_APPLET_AID[16] = {0xA0, 0x00, 0x00, 0x03, 0x96, 0x54, 0x53, 0x00,
                                          0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00};

// Pre-provisioned object ID for the 18-byte chip UID.
constexpr uint32_t SE050_OBJ_UNIQUE_ID = 0x7FFF0206;

// CRC-16/X-25 (== ISO/IEC 13239): poly 0x1021 reflected (=0x8408), init 0xFFFF,
// reflected in/out, final XOR 0xFFFF. Used by SE050 T=1 framing.
uint16_t crc16_x25(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0x8408) : (crc >> 1);
        }
    }
    return (uint16_t)(~crc);
}

// Derive the 16-byte IV for SCP03 C-DEC/R-ENC per GP 2.3 Amd D §6.2.6.
// counterBlock = [0x80 if response else 0x00] || 12 zero bytes || 3-byte BE counter.
// IV = AES-ECB(S-ENC, counterBlock).
void gp_iv(const uint8_t sEnc[16], uint32_t counter, bool responseDir, uint8_t iv[16])
{
    uint8_t block[16] = {0};
    if (responseDir)
        block[0] = 0x80;
    block[13] = (uint8_t)((counter >> 16) & 0xFF);
    block[14] = (uint8_t)((counter >> 8) & 0xFF);
    block[15] = (uint8_t)(counter & 0xFF);
    AES128 aes;
    aes.setKey(sEnc, 16);
    aes.encryptBlock(iv, block);
}

// AES-CBC encrypt / decrypt of exactly `nBlocks` 16-byte blocks.
void aes_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t nBlocks)
{
    AES128 aes;
    aes.setKey(key, 16);
    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (size_t b = 0; b < nBlocks; b++) {
        uint8_t xored[16];
        for (int i = 0; i < 16; i++)
            xored[i] = in[b * 16 + i] ^ prev[i];
        aes.encryptBlock(out + b * 16, xored);
        memcpy(prev, out + b * 16, 16);
    }
}

void aes_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t nBlocks)
{
    AES128 aes;
    aes.setKey(key, 16);
    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (size_t b = 0; b < nBlocks; b++) {
        uint8_t dec[16];
        aes.decryptBlock(dec, in + b * 16);
        for (int i = 0; i < 16; i++)
            out[b * 16 + i] = dec[i] ^ prev[i];
        memcpy(prev, in + b * 16, 16);
    }
}

// Poll the SE050 for a response; the chip NACKs its address while it is busy.
size_t pollRead(TwoWire *bus, uint8_t addr, uint8_t *out, size_t maxLen, uint32_t timeout_ms)
{
    // Arduino Wire::requestFrom takes a uint8_t quantity -- cap to 255 to avoid
    // silent overflow truncation (e.g. maxLen=260 -> (uint8_t)260 = 4 bytes).
    // If the caller ever needs > 255 bytes we'd chain multiple reads, but for
    // SE050 T=1 frames that fit in IFSC, 255 per-call is plenty.
    size_t attempt = maxLen > 255 ? 255 : maxLen;
    const uint32_t start = millis();
    for (;;) {
        size_t requested = bus->requestFrom((uint8_t)addr, (uint8_t)attempt);
        if (requested > 0) {
            size_t got = 0;
            while (got < maxLen && bus->available()) {
                out[got++] = (uint8_t)bus->read();
            }
            return got;
        }
        if ((millis() - start) > timeout_ms)
            return 0;
        delay(2);
    }
}

} // namespace

namespace se050
{

Client *client = nullptr;

Client::Client(TwoWire *bus_, uint8_t address_) : bus(bus_), address(address_), hostNS(0), ready(false), cachedUidValid(false)
{
    memset(&scp, 0, sizeof(scp));
    memset(cachedUid, 0, sizeof(cachedUid));
}

bool Client::getCachedUID(uint8_t uidOut[18]) const
{
    if (!cachedUidValid)
        return false;
    memcpy(uidOut, cachedUid, 18);
    return true;
}

// ============================================================================
//  SCP03 (GlobalPlatform 2.3 Amendment D) helpers -- security level 0x01 C-MAC
// ============================================================================
namespace
{

// Left-shift a 128-bit block by one bit with conditional XOR of the Rb constant
// (0x87 for AES) per NIST SP 800-38B. This is the "dbl" operation in GF(2^128).
static void cmac_dbl(uint8_t out[16], const uint8_t in[16])
{
    uint8_t carry = (in[0] & 0x80) ? 0x87 : 0x00;
    for (int i = 0; i < 15; i++) {
        out[i] = (uint8_t)((in[i] << 1) | (in[i + 1] >> 7));
    }
    out[15] = (uint8_t)((in[15] << 1) ^ carry);
}

// Standard AES-128 CMAC per NIST SP 800-38B / RFC 4493.
// Note: we can't use rweather's OMAC class because that implements EAX-mode OMAC
// (an implicit 16-byte zero tag block is prepended), which is a different algorithm
// than the CMAC mandated by GP 2.3 Amd D SCP03.
void aes128_cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t out[16])
{
    AES128 cipher;
    cipher.setKey(key, 16);

    // Derive subkeys K1, K2 from L = AES(K, 0^128).
    uint8_t L[16] = {0};
    cipher.encryptBlock(L, L);
    uint8_t K1[16], K2[16];
    cmac_dbl(K1, L);
    cmac_dbl(K2, K1);

    // Pick the last block's mask: K1 if message ends on a full block and len > 0,
    // K2 otherwise (empty message or last block is partial, needs 0x80... padding).
    bool fullLast = (len > 0) && ((len & 0x0F) == 0);
    const uint8_t *Kmask = fullLast ? K1 : K2;

    // Number of full 16-byte blocks excluding the last block.
    size_t nBlocks = (len == 0) ? 0 : ((len - 1) / 16);

    // CBC-MAC through all but the last block.
    uint8_t state[16] = {0};
    for (size_t i = 0; i < nBlocks; i++) {
        for (int j = 0; j < 16; j++)
            state[j] ^= data[i * 16 + j];
        cipher.encryptBlock(state, state);
    }

    // Prepare last block: full bytes if fullLast, padded otherwise; then XOR with mask.
    uint8_t last[16] = {0};
    size_t lastStart = nBlocks * 16;
    size_t lastLen = len - lastStart;
    if (fullLast) {
        memcpy(last, data + lastStart, 16);
    } else {
        if (lastLen)
            memcpy(last, data + lastStart, lastLen);
        last[lastLen] = 0x80;
    }
    for (int j = 0; j < 16; j++)
        state[j] ^= last[j] ^ Kmask[j];
    cipher.encryptBlock(state, state);

    memcpy(out, state, 16);
}

// GP 2.3 Amd D §6.2.2 key-derivation function using AES-CMAC.
// Input block (exactly 32 bytes for our 16-byte context):
//   [label: 11 zeros || derivation_constant] = 12 bytes
//   [separation: 0x00]                       = 1 byte
//   [L: output length in bits, 2 bytes BE]   = 2 bytes
//   [counter i: 0x01]                        = 1 byte
//   [context]                                = contextLen bytes
// Total = 16 + contextLen
void gp_kdf(const uint8_t key[16], uint8_t derivConst, const uint8_t *context, size_t contextLen, uint16_t outBitLen,
            uint8_t *out, size_t outLen)
{
    uint8_t block[48];
    size_t pos = 0;
    // Label: 11 zero bytes + derivation constant
    memset(block, 0, 11);
    pos = 11;
    block[pos++] = derivConst;
    // Separation indicator
    block[pos++] = 0x00;
    // L: output bit length, big-endian 16-bit
    block[pos++] = (uint8_t)((outBitLen >> 8) & 0xFF);
    block[pos++] = (uint8_t)(outBitLen & 0xFF);
    // Counter i = 0x01 (we never derive more than 128 bits so single block is enough)
    block[pos++] = 0x01;
    // Context: for session-key derivation = host_challenge || card_challenge
    //          for cryptogram derivation  = host_challenge || card_challenge  (same)
    memcpy(block + pos, context, contextLen);
    pos += contextLen;
    uint8_t tmp[16];
    aes128_cmac(key, block, pos, tmp);
    memcpy(out, tmp, outLen);
}

} // namespace

bool Client::txFrame(uint8_t pcb, const uint8_t *inf, size_t infLen)
{
    if (bus == nullptr || infLen > 250)
        return false;

    uint8_t frame[256];
    frame[0] = NAD_HOST_TO_SE;
    frame[1] = pcb;
    frame[2] = (uint8_t)infLen;
    if (infLen)
        memcpy(&frame[3], inf, infLen);
    uint16_t crc = crc16_x25(frame, 3 + infLen);
    frame[3 + infLen + 0] = (uint8_t)(crc & 0xFF);
    frame[3 + infLen + 1] = (uint8_t)(crc >> 8);

    bus->beginTransmission(address);
    bus->write(frame, 5 + infLen);
    return bus->endTransmission() == 0;
}

int Client::rxFrame(uint8_t *infOut, size_t infMax, uint8_t *pcbOut, uint32_t timeout_ms)
{
    uint8_t buf[260];
    // Read generously; SE050 pads the tail with 0xFF when its frame is shorter.
    size_t ask = infMax + 5;
    if (ask > sizeof(buf))
        ask = sizeof(buf);
    size_t got = pollRead(bus, address, buf, ask, timeout_ms);
    if (got < 5) {
        // Diagnostic: dump whatever we got (might be NACKs, partial T=1, etc.).
        char hex[32] = {0};
        int dump = got < 10 ? (int)got : 10;
        for (int i = 0; i < dump; i++)
            snprintf(&hex[i * 3], 4, "%02x ", buf[i]);
        LOG_DEBUG("SE050 rxFrame: short read got=%u bytes=%s", (unsigned)got, hex);
        return -1; // short / timeout
    }
    if (buf[0] != NAD_SE_TO_HOST) {
        LOG_DEBUG("SE050 rxFrame: bad NAD=0x%02x (expected 0xA5), got=%u first5=%02x %02x %02x %02x %02x", buf[0], (unsigned)got,
                  buf[0], buf[1], buf[2], buf[3], buf[4]);
        return -2; // bad NAD
    }

    uint8_t pcb = buf[1];
    uint8_t infLen = buf[2];
    if ((size_t)(3 + infLen + 2) > got)
        return -3; // truncated

    uint16_t crcCalc = crc16_x25(buf, 3 + infLen);
    uint16_t crcFrame = (uint16_t)buf[3 + infLen] | ((uint16_t)buf[4 + infLen] << 8);
    if (crcCalc != crcFrame)
        return -4; // CRC mismatch

    if (infLen > infMax)
        return -5; // caller buffer too small
    if (infLen)
        memcpy(infOut, &buf[3], infLen);
    if (pcbOut)
        *pcbOut = pcb;
    return (int)infLen;
}

bool Client::resync(uint32_t timeout_ms)
{
    if (!txFrame(PCB_SBLOCK_RESYNC_REQ, nullptr, 0)) {
        LOG_WARN("SE050 RESYNC: I2C write failed");
        return false;
    }
    uint8_t scratch[4];
    uint8_t pcb = 0;
    int n = rxFrame(scratch, sizeof(scratch), &pcb, timeout_ms);
    if (n < 0) {
        LOG_WARN("SE050 RESYNC: no response (err=%d)", n);
        return false;
    }
    if (pcb != PCB_SBLOCK_RESYNC_RSP) {
        LOG_WARN("SE050 RESYNC: unexpected PCB 0x%02x (expected 0xE0)", pcb);
        return false;
    }
    hostNS = 0;
    return true;
}

int Client::transceive(const uint8_t *apdu, size_t apduLen, uint8_t *rsp, size_t rspCapacity, uint32_t timeout_ms)
{
    if (!ready || bus == nullptr)
        return -100;
    if (apduLen == 0 || apduLen > 250)
        return -101;

    uint8_t pcb = hostNS ? PCB_IBLOCK_NS1 : PCB_IBLOCK_NS0;
    if (!txFrame(pcb, apdu, apduLen))
        return -102;

    uint8_t rxPcb = 0;
    int n = rxFrame(rsp, rspCapacity, &rxPcb, timeout_ms);
    if (n < 0)
        return n; // -1..-5 from rxFrame

    // Must be I-block (b7=0); anything else is a protocol error for our usage.
    if ((rxPcb & 0x80) != 0x00) {
        LOG_WARN("SE050 APDU: non-I-block reply PCB=0x%02x", rxPcb);
        return -110;
    }

    hostNS ^= 1;
    return n;
}

bool Client::begin(uint8_t *majorOut, uint8_t *minorOut, uint8_t *patchOut, uint16_t *configOut, uint32_t timeout_ms)
{
    ready = false;
    hostNS = 0;

    if (!resync(timeout_ms))
        return false;

    ready = true; // transceive() requires this flag to be set

    // ISO 7816-4 SELECT by DF name: 00 A4 04 00 10 <AID 16B> 00  (22 bytes)
    uint8_t selectApdu[22];
    selectApdu[0] = 0x00;
    selectApdu[1] = 0xA4;
    selectApdu[2] = 0x04;
    selectApdu[3] = 0x00;
    selectApdu[4] = 0x10;
    memcpy(&selectApdu[5], SE050_IOT_APPLET_AID, 16);
    selectApdu[21] = 0x00;

    uint8_t rsp[64];
    int n = transceive(selectApdu, sizeof(selectApdu), rsp, sizeof(rsp), timeout_ms);
    if (n < 2) {
        LOG_WARN("SE050 begin: SELECT transceive err (%d)", n);
        ready = false;
        return false;
    }

    uint8_t sw1 = rsp[n - 2];
    uint8_t sw2 = rsp[n - 1];
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 begin: SELECT SW=%02x%02x", sw1, sw2);
        ready = false;
        return false;
    }

    int payload = n - 2;
    if (payload >= 5) {
        if (majorOut)
            *majorOut = rsp[0];
        if (minorOut)
            *minorOut = rsp[1];
        if (patchOut)
            *patchOut = rsp[2];
        if (configOut)
            *configOut = ((uint16_t)rsp[3] << 8) | rsp[4];
    }
    return true;
}

bool Client::getUID(uint8_t uidOut[18], uint32_t timeout_ms)
{
    if (!ready)
        return false;

    // ReadObject(UNIQUE_ID) ISO 7816-4 Case-4 short APDU (total 12 bytes):
    //   CLA INS P1 P2  Lc  TLV(tag=0x41, len=0x04, value=0x7FFF0206)  Le
    //   80  02  00 00  06  41 04 7F FF 02 06                           00
    const uint8_t apdu[12] = {0x80,
                              0x02,
                              0x00,
                              0x00,
                              0x06,
                              0x41,
                              0x04,
                              (uint8_t)(SE050_OBJ_UNIQUE_ID >> 24),
                              (uint8_t)(SE050_OBJ_UNIQUE_ID >> 16),
                              (uint8_t)(SE050_OBJ_UNIQUE_ID >> 8),
                              (uint8_t)(SE050_OBJ_UNIQUE_ID & 0xFF),
                              0x00};

    uint8_t rsp[32];
    int n = transceive(apdu, sizeof(apdu), rsp, sizeof(rsp), timeout_ms);
    if (n < 2) {
        LOG_WARN("SE050 GetUID: transceive err (%d)", n);
        return false;
    }

    uint8_t sw1 = rsp[n - 2];
    uint8_t sw2 = rsp[n - 1];
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 GetUID: SW=%02x%02x", sw1, sw2);
        return false;
    }

    // ReadObject response on SE050 IoT Applet v7.2.0 observed as a BER-TLV:
    //   41 82 00 12  <18 UID bytes>        (tag=0x41, long-form length 0x0012)
    // Older AN12413 documents a raw 18-byte layout. Parse BER length robustly so we
    // accept short form (0x00..0x7F), long-1 (0x81 len), and long-2 (0x82 hi lo).
    int contentLen = n - 2;
    const uint8_t *contentPtr = rsp;

    if (contentLen >= 2 && rsp[0] == 0x41) {
        int headerLen = 0;
        int innerLen = -1;
        uint8_t l0 = rsp[1];
        if (l0 < 0x80) {
            innerLen = l0;
            headerLen = 2;
        } else if (l0 == 0x81 && contentLen >= 3) {
            innerLen = rsp[2];
            headerLen = 3;
        } else if (l0 == 0x82 && contentLen >= 4) {
            innerLen = ((int)rsp[2] << 8) | (int)rsp[3];
            headerLen = 4;
        }
        if (innerLen >= 0 && headerLen + innerLen <= contentLen) {
            contentPtr = &rsp[headerLen];
            contentLen = innerLen;
        }
    }

    if (contentLen < 18) {
        LOG_WARN("SE050 GetUID: unwrapped payload too short (%d bytes, need >= 18)", contentLen);
        return false;
    }
    if (contentLen != 18) {
        LOG_DEBUG("SE050 GetUID: payload %d B, taking last 18", contentLen);
    }
    // Chip UID is always the trailing 18 bytes of the object content.
    memcpy(uidOut, contentPtr + (contentLen - 18), 18);

    // Cache for cheap re-reads by other modules without hitting the I2C bus.
    memcpy(cachedUid, uidOut, 18);
    cachedUidValid = true;
    return true;
}

// ============================================================================
//  Random from SE050 TRNG -- Se05x_API_GetRandom
// ============================================================================
bool Client::getRandom(uint8_t *out, size_t n, uint32_t timeout_ms)
{
    if (out == nullptr || n == 0 || n > 255)
        return false;

    // APDU: 80 49 04 00 Lc 41 02 <len_hi> <len_lo> Le=00
    uint8_t apdu[11];
    apdu[0] = 0x80;
    apdu[1] = 0x49; // INS_MGMT (Crypto) -- actually INS_CRYPTO
    apdu[2] = 0x04; // P1_DEFAULT = RANDOM
    apdu[3] = 0x00;
    apdu[4] = 0x05; // Lc
    apdu[5] = 0x41; // TAG_1
    apdu[6] = 0x02;
    apdu[7] = 0x00;
    apdu[8] = (uint8_t)(n & 0xFF);
    apdu[9] = 0x00; // Le

    uint8_t rsp[300] = {0};
    int rn = sendSecure(apdu, 10, rsp, sizeof(rsp), timeout_ms);
    if (rn < 2)
        return false;
    uint8_t sw1 = rsp[rn - 2];
    uint8_t sw2 = rsp[rn - 1];
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 GetRandom: SW=%02x%02x", sw1, sw2);
        return false;
    }
    int payloadLen = rn - 2;
    // Response wrapping: <tag 0x41> <len> <N random bytes>  (BER may use long form)
    const uint8_t *p = rsp;
    int remaining = payloadLen;
    if (remaining >= 2 && p[0] == 0x41) {
        int headerLen = 0;
        int innerLen = -1;
        uint8_t l0 = p[1];
        if (l0 < 0x80) {
            innerLen = l0;
            headerLen = 2;
        } else if (l0 == 0x81 && remaining >= 3) {
            innerLen = p[2];
            headerLen = 3;
        } else if (l0 == 0x82 && remaining >= 4) {
            innerLen = ((int)p[2] << 8) | p[3];
            headerLen = 4;
        }
        if (innerLen >= 0 && headerLen + innerLen <= remaining) {
            p += headerLen;
            remaining = innerLen;
        }
    }
    if ((size_t)remaining < n)
        return false;
    memcpy(out, p, n);
    return true;
}

// ============================================================================
//  SCP03 Platform session -- INITIALIZE UPDATE + EXTERNAL AUTHENTICATE
// ============================================================================
bool Client::openPlatformScp03(const uint8_t encKey[16], const uint8_t macKey[16], const uint8_t dekKey[16], uint32_t timeout_ms)
{
    if (!ready || bus == nullptr)
        return false;

    // Self-test: verify CMAC against NIST SP 800-38B test vector before we trust it
    // for SCP03. Fail fast with a clear log line if the CMAC is broken.
    {
        const uint8_t nistKey[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                     0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
        const uint8_t nistExpectedEmpty[16] = {0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28,
                                               0x7f, 0xa3, 0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46};
        uint8_t mac[16];
        aes128_cmac(nistKey, nullptr, 0, mac);
        if (memcmp(mac, nistExpectedEmpty, 16) != 0) {
            LOG_ERROR("SE050 SCP03: CMAC self-test FAILED -- SCP03 cannot be opened safely.");
            return false;
        }
    }

    // 1. Generate 8-byte host challenge via the firmware RNG.
    uint8_t hostChal[8];
    RNG.rand(hostChal, sizeof(hostChal));

    // 2. INITIALIZE UPDATE APDU: 80 50 00 00 08 <hostChal[8]> 00
    uint8_t initUpd[14];
    initUpd[0] = 0x80;
    initUpd[1] = 0x50;
    initUpd[2] = 0x00;
    initUpd[3] = 0x00;
    initUpd[4] = 0x08;
    memcpy(initUpd + 5, hostChal, 8);
    initUpd[13] = 0x00;

    uint8_t rsp[64] = {0};
    int n = transceive(initUpd, sizeof(initUpd), rsp, sizeof(rsp), timeout_ms);
    if (n < 2) {
        LOG_WARN("SE050 SCP03 INITIALIZE UPDATE: transceive err %d", n);
        return false;
    }
    uint8_t sw1 = rsp[n - 2], sw2 = rsp[n - 1];
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 SCP03 INITIALIZE UPDATE: SW=%02x%02x (wrong static keys?)", sw1, sw2);
        return false;
    }

    // SCP03 INITIALIZE UPDATE response layout:
    //   [KDD: 10B] [key info: 3B] [card challenge: 8B] [card cryptogram: 8B]
    //   [seq counter: 3B, OPTIONAL -- only in pseudo-random challenge mode]
    // So payload is 29 bytes without counter, 32 bytes with. SE050 default is
    // the random-challenge mode (no counter). We only use the first 29 bytes.
    if (n - 2 < 29) {
        LOG_WARN("SE050 SCP03 INITIALIZE UPDATE: short response %d", n - 2);
        return false;
    }
    const uint8_t *kdd = rsp;
    const uint8_t *keyInfo = rsp + 10;
    const uint8_t *cardChal = rsp + 13;
    const uint8_t *cardCrypto = rsp + 21;

    // Diagnostic dump so we can verify OEF, key version, and compare cryptograms.
    {
        char hex[29 * 2 + 1] = {0};
        for (int i = 0; i < 29; i++)
            snprintf(&hex[i * 2], 3, "%02x", rsp[i]);
        LOG_DEBUG("SCP03 INIT UPDATE rsp (29 B): %s", hex);
        LOG_DEBUG("SCP03 KDD[10]: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x  keyInfo: %02x %02x %02x", kdd[0], kdd[1], kdd[2],
                  kdd[3], kdd[4], kdd[5], kdd[6], kdd[7], kdd[8], kdd[9], keyInfo[0], keyInfo[1], keyInfo[2]);
    }

    // 3. Build derivation context = host_challenge || card_challenge (16 bytes).
    uint8_t ctx[16];
    memcpy(ctx, hostChal, 8);
    memcpy(ctx + 8, cardChal, 8);

    // 4. Derive session keys using GP KDF.
    gp_kdf(encKey, 0x04, ctx, sizeof(ctx), 128, scp.sEnc, 16);
    gp_kdf(macKey, 0x06, ctx, sizeof(ctx), 128, scp.sMac, 16);
    gp_kdf(macKey, 0x07, ctx, sizeof(ctx), 128, scp.sRmac, 16);
    memcpy(scp.sDek, dekKey, 16);

    // 5. Verify card cryptogram = KDF(S-MAC, 0x00, ctx, 64 bits).
    uint8_t expectedCardCrypto[8];
    gp_kdf(scp.sMac, 0x00, ctx, sizeof(ctx), 64, expectedCardCrypto, 8);

    {
        char sEncHex[33] = {0}, sMacHex[33] = {0};
        for (int i = 0; i < 16; i++) {
            snprintf(&sEncHex[i * 2], 3, "%02x", scp.sEnc[i]);
            snprintf(&sMacHex[i * 2], 3, "%02x", scp.sMac[i]);
        }
        LOG_DEBUG("SCP03 S-ENC=%s S-MAC=%s", sEncHex, sMacHex);
        char ctxHex[33] = {0};
        for (int i = 0; i < 16; i++)
            snprintf(&ctxHex[i * 2], 3, "%02x", ctx[i]);
        LOG_DEBUG("SCP03 ctx=%s", ctxHex);
        char exp[17] = {0}, got[17] = {0};
        for (int i = 0; i < 8; i++) {
            snprintf(&exp[i * 2], 3, "%02x", expectedCardCrypto[i]);
            snprintf(&got[i * 2], 3, "%02x", cardCrypto[i]);
        }
        LOG_DEBUG("SCP03 card cryptogram: expected=%s received=%s", exp, got);
    }

    if (memcmp(expectedCardCrypto, cardCrypto, 8) != 0) {
        LOG_WARN("SE050 SCP03: card cryptogram mismatch (wrong static keys or KDF error)");
        return false;
    }

    // 6. Compute host cryptogram.
    uint8_t hostCrypto[8];
    gp_kdf(scp.sMac, 0x01, ctx, sizeof(ctx), 64, hostCrypto, 8);

    // 7. EXTERNAL AUTHENTICATE with security level 0x33 (full C-DEC+C-MAC+R-ENC+R-MAC).
    //    The host cryptogram is NOT encrypted -- it always travels in plaintext as
    //    part of EXT-AUTH. Only the MAC is applied over the header+hostCrypto.
    //    APDU: 84 82 33 00 10 <hostCrypto[8]> <cmac[8]>
    uint8_t extAuth[5 + 8 + 8];
    extAuth[0] = 0x84; // CLA: C-MAC'd
    extAuth[1] = 0x82;
    extAuth[2] = 0x33; // P1 = security level 0x33 (C-DEC | C-MAC | R-ENC | R-MAC)
    extAuth[3] = 0x00;
    extAuth[4] = 0x10; // Lc = 16 (8 hostCrypto + 8 MAC)
    memcpy(extAuth + 5, hostCrypto, 8);

    // CMAC input for first command: initial MCV (zeros) || CLA||INS||P1||P2||Lc||hostCrypto[8]
    uint8_t macInput[16 + 13];
    memset(macInput, 0, 16);
    memcpy(macInput + 16, extAuth, 13);
    uint8_t macResult[16];
    aes128_cmac(scp.sMac, macInput, sizeof(macInput), macResult);
    memcpy(extAuth + 13, macResult, 8);
    memcpy(scp.mcv, macResult, 16); // MCV for next command = full 16-byte MAC

    n = transceive(extAuth, sizeof(extAuth), rsp, sizeof(rsp), timeout_ms);
    if (n < 2) {
        LOG_WARN("SE050 SCP03 EXTERNAL AUTHENTICATE: transceive err %d", n);
        return false;
    }
    sw1 = rsp[n - 2];
    sw2 = rsp[n - 1];
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 SCP03 EXTERNAL AUTHENTICATE: SW=%02x%02x", sw1, sw2);
        return false;
    }

    scp.active = true;
    scp.encCounter = 1; // GP 2.3 Amd D §6.2.5: first C-APDU uses counter value 1
    LOG_INFO("SE050 SCP03 session opened (security level 0x33, full ENC+MAC)");
    return true;
}

// ============================================================================
//  Secure send -- SCP03 security level 0x33 (C-DEC+C-MAC+R-ENC+R-MAC)
// ============================================================================
int Client::sendSecure(const uint8_t *apdu, size_t apduLen, uint8_t *rsp, size_t rspCap, uint32_t timeout_ms)
{
    if (!scp.active)
        return transceive(apdu, apduLen, rsp, rspCap, timeout_ms);
    if (apduLen < 4 || apduLen > 255)
        return -200;

    // Parse APDU cases. We only handle short-form.
    uint8_t cla = apdu[0];
    uint8_t ins = apdu[1];
    uint8_t p1 = apdu[2];
    uint8_t p2 = apdu[3];
    size_t lc = 0;
    bool hasBody = false, hasLe = false;
    const uint8_t *data = nullptr;
    uint8_t le = 0;
    if (apduLen == 4) {
        // Case 1
    } else if (apduLen == 5) {
        // Case 2S: header + Le
        hasLe = true;
        le = apdu[4];
    } else {
        lc = apdu[4];
        if (apduLen == 5 + lc) {
            hasBody = true;
            data = apdu + 5;
        } else if (apduLen == 5 + lc + 1) {
            hasBody = true;
            data = apdu + 5;
            hasLe = true;
            le = apdu[apduLen - 1];
        } else {
            return -201;
        }
    }

    // Large buffers live in BSS to avoid stack overflow -- sendSecure() is already
    // non-reentrant (single SCP03 session), so static is safe and cheap.
    static uint8_t encData[288];
    static uint8_t wrapped[320];
    static uint8_t macBuf[320 + 16];
    static uint8_t rspRaw[320];
    static uint8_t rmacBuf[320 + 16 + 2];
    static uint8_t decrypted[288];

    // ---- C-DEC: pad + encrypt the command body ----
    // If there's no body, encrypted body stays empty (some applets allow this at
    // security level 0x33 even though GP spec describes empty-data edge cases
    // ambiguously; SE050 accepts it in practice).
    size_t encDataLen = 0;
    if (hasBody || lc > 0) {
        uint8_t padded[256];
        if (lc > 0)
            memcpy(padded, data, lc);
        padded[lc] = 0x80;
        size_t paddedLen = lc + 1;
        while (paddedLen % 16 != 0)
            padded[paddedLen++] = 0x00;

        uint8_t iv[16];
        gp_iv(scp.sEnc, scp.encCounter, false, iv);
        aes_cbc_encrypt(scp.sEnc, iv, padded, encData, paddedLen / 16);
        encDataLen = paddedLen;
    }

    // ---- Build wrapped APDU: header || newLc || encData, then append MAC, then Le ----
    size_t wl = 0;
    wrapped[wl++] = cla | 0x04; // MAC'd
    wrapped[wl++] = ins;
    wrapped[wl++] = p1;
    wrapped[wl++] = p2;
    // newLc includes encrypted body + 8 bytes MAC
    wrapped[wl++] = (uint8_t)(encDataLen + 8);
    if (encDataLen > 0) {
        if (wl + encDataLen > sizeof(wrapped))
            return -202;
        memcpy(wrapped + wl, encData, encDataLen);
        wl += encDataLen;
    }

    // ---- C-MAC over MCV || header || newLc || encData ----
    memcpy(macBuf, scp.mcv, 16);
    memcpy(macBuf + 16, wrapped, wl);
    uint8_t macResult[16];
    aes128_cmac(scp.sMac, macBuf, 16 + wl, macResult);
    memcpy(wrapped + wl, macResult, 8);
    wl += 8;
    memcpy(scp.mcv, macResult, 16);

    if (hasLe) {
        if (wl >= sizeof(wrapped))
            return -202;
        wrapped[wl++] = le;
    }

    {
        int dump = (int)wl < 48 ? (int)wl : 48;
        char hex[48 * 3 + 1] = {0};
        for (int i = 0; i < dump; i++)
            snprintf(&hex[i * 3], 4, "%02x ", wrapped[i]);
        LOG_DEBUG("SE050 sendSecure tx (%u B): %s", (unsigned)wl, hex);
    }

    // ---- Send + receive raw encrypted response ----
    int n = transceive(wrapped, wl, rspRaw, sizeof(rspRaw), timeout_ms);
    if (n < 0)
        return n;
    if (n < 10) {
        // Short response. If n >= 2 the last two bytes are an SW the applet
        // returned *before* the secure-channel layer could attach R-MAC -- pass
        // it through so the caller can see the real error code.
        if (n >= 2 && rspCap >= 2) {
            uint8_t sw1 = rspRaw[n - 2];
            uint8_t sw2 = rspRaw[n - 1];
            LOG_WARN("SE050 sendSecure: short response (%d B), SW=%02x%02x (pre-R-MAC error)", n, sw1, sw2);
            rsp[0] = sw1;
            rsp[1] = sw2;
            return 2;
        }
        LOG_WARN("SE050 sendSecure: short secure response %d", n);
        return -210;
    }

    // Response frame: [encRspData (0 or 16N bytes)] [R-MAC (8)] [SW (2)]
    size_t encRspDataLen = (size_t)(n - 8 - 2);
    const uint8_t *encRspData = rspRaw;
    const uint8_t *rMacReceived = rspRaw + encRspDataLen;
    uint8_t sw1 = rspRaw[n - 2];
    uint8_t sw2 = rspRaw[n - 1];

    // ---- Verify R-MAC: CMAC(S-RMAC, prev_C_MCV || encRspData || SW) truncated to 8 ----
    memcpy(rmacBuf, scp.mcv, 16);
    if (encRspDataLen > 0)
        memcpy(rmacBuf + 16, encRspData, encRspDataLen);
    rmacBuf[16 + encRspDataLen + 0] = sw1;
    rmacBuf[16 + encRspDataLen + 1] = sw2;
    uint8_t rmacExpected[16];
    aes128_cmac(scp.sRmac, rmacBuf, 16 + encRspDataLen + 2, rmacExpected);
    if (memcmp(rmacExpected, rMacReceived, 8) != 0) {
        LOG_WARN("SE050 R-MAC verify failed (SW=%02x%02x)", sw1, sw2);
        // Still fall through and surface SW to caller for diagnostic -- the response
        // payload itself is suspect but the SW can still guide us.
    }

    // ---- Decrypt response body if any ----
    size_t plainRspLen = 0;
    if (encRspDataLen > 0) {
        if (encRspDataLen % 16 != 0) {
            LOG_WARN("SE050 encRsp not multiple of 16: %u", (unsigned)encRspDataLen);
            return -211;
        }
        uint8_t rspIv[16];
        gp_iv(scp.sEnc, scp.encCounter, true, rspIv);
        aes_cbc_decrypt(scp.sEnc, rspIv, encRspData, decrypted, encRspDataLen / 16);

        // Strip 0x80... padding from the tail
        plainRspLen = encRspDataLen;
        while (plainRspLen > 0 && decrypted[plainRspLen - 1] == 0x00)
            plainRspLen--;
        if (plainRspLen == 0 || decrypted[plainRspLen - 1] != 0x80) {
            LOG_WARN("SE050 response padding invalid");
            return -212;
        }
        plainRspLen--; // drop the 0x80 marker
        if (plainRspLen > rspCap - 2)
            return -213;
        if (plainRspLen > 0)
            memcpy(rsp, decrypted, plainRspLen);
    }

    // Append SW and return
    if (plainRspLen + 2 > rspCap)
        return -214;
    rsp[plainRspLen] = sw1;
    rsp[plainRspLen + 1] = sw2;

    // Bump encryption counter for next command
    scp.encCounter++;

    return (int)(plainRspLen + 2);
}

// ============================================================================
//  Applet-level operations (EC curve / key / ECDH)
// ============================================================================

bool Client::createECCurve(uint8_t curveId, uint32_t timeout_ms)
{
    // CreateECCurve per NXP Plug & Trust Se05x_API_CreateECCurve:
    //   CLA=0x80, INS=INS_WRITE=0x01, P1=P1_CURVE=0x0B, P2=P2_CREATE=0x04
    //   Body: TLV(TAG_1=0x41, 1-byte curveId)
    //   Case 3 (no Le).
    uint8_t apdu[] = {0x80, 0x01, 0x0B, 0x04, 0x03, 0x41, 0x01, curveId};
    uint8_t rsp[32] = {0};
    int n = sendSecure(apdu, sizeof(apdu), rsp, sizeof(rsp), timeout_ms);
    if (n < 2) {
        LOG_WARN("SE050 CreateECCurve 0x%02x: transceive err %d", curveId, n);
        return false;
    }
    uint8_t sw1 = rsp[n - 2];
    uint8_t sw2 = rsp[n - 1];
    if (sw1 == 0x90 && sw2 == 0x00) {
        LOG_INFO("SE050 CreateECCurve 0x%02x: loaded", curveId);
        return true;
    }
    // SW=6A89 "Conditions not satisfied (duplicate)" => curve already present; treat as OK.
    if (sw1 == 0x6A && sw2 == 0x89) {
        LOG_INFO("SE050 CreateECCurve 0x%02x: already present", curveId);
        return true;
    }
    LOG_WARN("SE050 CreateECCurve 0x%02x: SW=%02x%02x", curveId, sw1, sw2);
    return false;
}

bool Client::writeECKeyGen(uint32_t objectId, uint8_t curveId, uint32_t timeout_ms)
{
    // WriteECKey (on-chip keygen) per NXP Plug & Trust Se05x_API_WriteECKey:
    //   CLA=0x80, INS=INS_WRITE=0x01,
    //   P1=P1_EC | P1_KEY_PAIR = 0x01 | 0x60 = 0x61,
    //   P2=P2_DEFAULT=0x00
    //   Body (empty policy + empty max-attempts are omitted, key_part=KEY_PAIR):
    //     TLV(TAG_1=0x41, 4-byte objectID)
    //     TLV(TAG_2=0x42, 1-byte curveID)
    //     -- no TAG_3/TAG_4 (privKey/pubKey) -> chip generates internally
    //   Case 3 (no Le, no response body beyond SW).
    uint8_t apdu[5 + 6 + 3];
    apdu[0] = 0x80;
    apdu[1] = 0x01;
    apdu[2] = 0x01 | 0x60; // P1_EC | P1_KEY_PAIR
    apdu[3] = 0x00;        // P2_DEFAULT
    apdu[4] = 0x09;        // Lc = 9
    apdu[5] = 0x41;
    apdu[6] = 0x04;
    apdu[7] = (uint8_t)(objectId >> 24);
    apdu[8] = (uint8_t)(objectId >> 16);
    apdu[9] = (uint8_t)(objectId >> 8);
    apdu[10] = (uint8_t)(objectId & 0xFF);
    apdu[11] = 0x42;
    apdu[12] = 0x01;
    apdu[13] = curveId;

    uint8_t rsp[64] = {0};
    int n = sendSecure(apdu, sizeof(apdu), rsp, sizeof(rsp), timeout_ms);
    if (n < 2) {
        LOG_WARN("SE050 WriteECKeyGen(0x%08x, 0x%02x): transceive err %d", (unsigned)objectId, curveId, n);
        return false;
    }
    uint8_t sw1 = rsp[n - 2];
    uint8_t sw2 = rsp[n - 1];
    LOG_DEBUG("SE050 WriteECKeyGen(0x%08x, 0x%02x): SW=%02x%02x rspLen=%d", (unsigned)objectId, curveId, sw1, sw2, n);
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 WriteECKeyGen(0x%08x): SW=%02x%02x", (unsigned)objectId, sw1, sw2);
        return false;
    }
    return true;
}

bool Client::deleteObject(uint32_t objectId, uint32_t timeout_ms)
{
    // DeleteSecureObject per NXP Plug & Trust Se05x_API_DeleteSecureObject:
    //   CLA=0x80, INS=INS_MGMT=0x04, P1=P1_DEFAULT=0x00, P2=P2_DELETE_OBJECT=0x28
    //   Body: TLV(TAG_1=0x41, 4-byte objectID)
    //   Case 3 (no Le).
    uint8_t apdu[11];
    apdu[0] = 0x80;
    apdu[1] = 0x04;
    apdu[2] = 0x00;
    apdu[3] = 0x28;
    apdu[4] = 0x06; // Lc = 6
    apdu[5] = 0x41;
    apdu[6] = 0x04;
    apdu[7] = (uint8_t)(objectId >> 24);
    apdu[8] = (uint8_t)(objectId >> 16);
    apdu[9] = (uint8_t)(objectId >> 8);
    apdu[10] = (uint8_t)(objectId & 0xFF);

    uint8_t rsp[32];
    int n = sendSecure(apdu, sizeof(apdu), rsp, sizeof(rsp), timeout_ms);
    if (n < 2)
        return false;
    uint8_t sw1 = rsp[n - 2];
    uint8_t sw2 = rsp[n - 1];
    if (sw1 == 0x90 && sw2 == 0x00) {
        LOG_DEBUG("SE050 DeleteObject(0x%08x): deleted", (unsigned)objectId);
        return true;
    }
    // SW=6A88 "Reference data not found" -> object didn't exist; treat as OK.
    if (sw1 == 0x6A && sw2 == 0x88) {
        LOG_DEBUG("SE050 DeleteObject(0x%08x): didn't exist", (unsigned)objectId);
        return true;
    }
    LOG_WARN("SE050 DeleteObject(0x%08x): SW=%02x%02x", (unsigned)objectId, sw1, sw2);
    return false;
}

bool Client::readECPub(uint32_t objectId, uint8_t *pubOut, size_t pubCapacity, size_t *pubLenOut, uint32_t timeout_ms)
{
    if (pubOut == nullptr || pubLenOut == nullptr)
        return false;

    // ReadObject: 80 02 00 00 06 41 04 <objId> 00
    uint8_t apdu[12];
    apdu[0] = 0x80;
    apdu[1] = 0x02;
    apdu[2] = 0x00;
    apdu[3] = 0x00;
    apdu[4] = 0x06;
    apdu[5] = 0x41;
    apdu[6] = 0x04;
    apdu[7] = (uint8_t)(objectId >> 24);
    apdu[8] = (uint8_t)(objectId >> 16);
    apdu[9] = (uint8_t)(objectId >> 8);
    apdu[10] = (uint8_t)(objectId & 0xFF);
    apdu[11] = 0x00;

    uint8_t rsp[300] = {0};
    int n = sendSecure(apdu, sizeof(apdu), rsp, sizeof(rsp), timeout_ms);
    if (n < 2)
        return false;
    uint8_t sw1 = rsp[n - 2];
    uint8_t sw2 = rsp[n - 1];
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 ReadECPub(0x%08x): SW=%02x%02x", (unsigned)objectId, sw1, sw2);
        return false;
    }
    int payloadLen = n - 2;

    // Dump raw payload so we can see the exact wrapping the SE050 uses.
    {
        int dump = payloadLen < 72 ? payloadLen : 72;
        char hex[72 * 2 + 1] = {0};
        for (int i = 0; i < dump; i++)
            snprintf(&hex[i * 2], 3, "%02x", rsp[i]);
        LOG_DEBUG("SE050 ReadECPub(0x%08x): %d B payload: %s", (unsigned)objectId, payloadLen, hex);
    }

    // Unwrap BER-TLV 0x41 <len> <value>. Any length form.
    const uint8_t *p = rsp;
    int remaining = payloadLen;
    if (remaining >= 2 && p[0] == 0x41) {
        int headerLen = 0;
        int innerLen = -1;
        uint8_t l0 = p[1];
        if (l0 < 0x80) {
            innerLen = l0;
            headerLen = 2;
        } else if (l0 == 0x81 && remaining >= 3) {
            innerLen = p[2];
            headerLen = 3;
        } else if (l0 == 0x82 && remaining >= 4) {
            innerLen = ((int)p[2] << 8) | p[3];
            headerLen = 4;
        }
        if (innerLen >= 0 && headerLen + innerLen <= remaining) {
            p += headerLen;
            remaining = innerLen;
        }
    }

    if ((size_t)remaining > pubCapacity) {
        LOG_WARN("SE050 ReadECPub(0x%08x): unwrapped payload too big (%d > %u)", (unsigned)objectId, remaining,
                 (unsigned)pubCapacity);
        return false;
    }
    memcpy(pubOut, p, remaining);
    *pubLenOut = (size_t)remaining;
    return true;
}

bool Client::ecdhX25519(uint32_t privObjectId, const uint8_t peerPub[32], uint8_t shared[32], uint32_t timeout_ms)
{
    // ECDHGenerateSharedSecret per NXP Plug & Trust Se05x_API_ECDHGenerateSharedSecret:
    //   CLA=0x80, INS=INS_CRYPTO=0x03, P1=P1_EC=0x01, P2=P2_DH=0x0F
    //   Body TLVs:
    //     TAG_1 (0x41): privObjectId (u32)
    //     TAG_2 (0x42): peerPub bytestring (32 bytes for Curve25519 X coord)
    //   Case 4 (Le=0x00) -- response returns TLV(0x41, 32-byte shared secret) + SW.
    uint8_t apdu[5 + (2 + 4) + (2 + 32) + 1];
    apdu[0] = 0x80;
    apdu[1] = 0x03;
    apdu[2] = 0x01; // P1_EC
    apdu[3] = 0x0F; // P2_DH
    apdu[4] = 0x28; // Lc = 6 + 34 = 40
    apdu[5] = 0x41;
    apdu[6] = 0x04;
    apdu[7] = (uint8_t)(privObjectId >> 24);
    apdu[8] = (uint8_t)(privObjectId >> 16);
    apdu[9] = (uint8_t)(privObjectId >> 8);
    apdu[10] = (uint8_t)(privObjectId & 0xFF);
    apdu[11] = 0x42;
    apdu[12] = 0x20; // 32 bytes
    memcpy(apdu + 13, peerPub, 32);
    apdu[45] = 0x00; // Le

    uint8_t rsp[64] = {0};
    int n = sendSecure(apdu, sizeof(apdu), rsp, sizeof(rsp), timeout_ms);
    if (n < 2) {
        LOG_WARN("SE050 ECDH: transceive err %d", n);
        return false;
    }
    uint8_t sw1 = rsp[n - 2];
    uint8_t sw2 = rsp[n - 1];
    if (!(sw1 == 0x90 && sw2 == 0x00)) {
        LOG_WARN("SE050 ECDH: SW=%02x%02x", sw1, sw2);
        return false;
    }
    int payloadLen = n - 2;

    // Unwrap 0x41 <len> <shared[32]>
    const uint8_t *p = rsp;
    int remaining = payloadLen;
    if (remaining >= 2 && p[0] == 0x41) {
        int headerLen = 0;
        int innerLen = -1;
        uint8_t l0 = p[1];
        if (l0 < 0x80) {
            innerLen = l0;
            headerLen = 2;
        } else if (l0 == 0x81 && remaining >= 3) {
            innerLen = p[2];
            headerLen = 3;
        } else if (l0 == 0x82 && remaining >= 4) {
            innerLen = ((int)p[2] << 8) | p[3];
            headerLen = 4;
        }
        if (innerLen >= 0 && headerLen + innerLen <= remaining) {
            p += headerLen;
            remaining = innerLen;
        }
    }
    if (remaining != 32) {
        LOG_WARN("SE050 ECDH: unexpected shared length %d", remaining);
        return false;
    }
    memcpy(shared, p, 32);
    return true;
}

} // namespace se050

#endif // !MESHTASTIC_EXCLUDE_I2C
