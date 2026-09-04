#include "SE050.h"

#if defined(HAS_SE050)

#include "configuration.h"
#include "mesh/HardwareRNG.h"
#include <AES.h>
#include <Arduino.h>
#include <Curve25519.h>
#include <string.h>

SE050 *se050 = nullptr;

#ifdef ARCH_RP2040
#include <hardware/watchdog.h>
#include <pico/time.h>
#define SE050_FEED_WATCHDOG() watchdog_update()
// A hardware busy-wait, not delay(). delay() runs the framework's yield hook,
// which hands control to whatever else is pending - in the middle of a
// transaction, that is exactly what must not happen. The wait is short and the
// watchdog is fed explicitly around it.
#define SE050_WAIT_MS(ms) busy_wait_us_32((ms)*1000u)
#else
#define SE050_FEED_WATCHDOG() ((void)0)
#define SE050_WAIT_MS(ms) delay(ms)
#endif

namespace
{
constexpr uint8_t NAD_HOST_TO_SE = 0x5A;
constexpr uint8_t NAD_SE_TO_HOST = 0xA5; // also the SOF we resynchronise on
constexpr uint8_t PCB_S_REQ = 0xC0;
constexpr uint8_t PCB_S_RSP = 0xE0;
constexpr uint8_t S_INTF_RESET = 0x0F;
constexpr uint8_t S_WTX = 0x03; // wait-time extension request/response

// The answer to a fresh key generation can take seconds (compute plus an NVM
// write), so the poll window has to be generous. Not-ready simply NACKs, which
// returns immediately, so a normal answer still exits after a couple of passes.
constexpr int POLL_ATTEMPTS = 400;
constexpr uint32_t POLL_INTERVAL_MS = 10;

// How many wait-time extensions to grant before declaring the chip stuck.
constexpr int MAX_WTX_GRANTS = 20;
} // namespace

// CRC-16 as T1oI2C uses it: reflected polynomial 0x8408, init and xorout 0xFFFF,
// and the result byte-swapped (UM11225), appended big-endian.
uint16_t SE050::crc(const uint8_t *data, size_t len)
{
    uint16_t cal = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        cal ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            cal = (cal & 1) ? ((cal >> 1) ^ 0x8408) : (cal >> 1);
    }
    cal ^= 0xFFFF;
    return (uint16_t)(((cal & 0xFF) << 8) | ((cal >> 8) & 0xFF));
}

// Whether a call arrived while a transaction is parked mid-flight. Refusing is
// the only safe answer: the caller would share buffers and an SCP03 counter with
// work that has not finished, and the chip would see two interleaved commands on
// one channel.
bool SE050::reentered(const char *what)
{
    if (!waiting)
        return false;
    LOG_ERROR("SE050: %s re-entered while a transaction is in flight, refusing", what);
    return true;
}

size_t SE050::xfer(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxCap)
{
    if (rxCap < 3)
        return 0;

    bus.beginTransmission(address);
    bus.write(tx, txLen);
    if (bus.endTransmission() != 0) {
        LOG_DEBUG("SE050: write of %u bytes was not acked", (unsigned)txLen);
        return 0;
    }

    // Read in two phases, the way NXP's own PAL does it: poll for the 3-byte
    // header until the start-of-frame shows up, then read exactly the body the
    // header announced. A single large fixed read does not survive a slow answer.
    uint8_t header[3];
    bool haveHeader = false;
    waiting = true;
    for (int attempt = 0; attempt < POLL_ATTEMPTS && !haveHeader; attempt++) {
        SE050_WAIT_MS(POLL_INTERVAL_MS);
        SE050_FEED_WATCHDOG(); // this loop can run for seconds
        if (bus.requestFrom(address, sizeof(header)) == sizeof(header)) {
            for (size_t i = 0; i < sizeof(header); i++)
                header[i] = bus.read();
            haveHeader = (header[0] == NAD_SE_TO_HOST);
        }
    }
    waiting = false;
    if (!haveHeader)
        return 0;

    rx[0] = header[0];
    rx[1] = header[1];
    rx[2] = header[2];

    size_t body = (size_t)header[2] + 2; // INF plus the two CRC bytes
    if (3 + body > rxCap) {
        LOG_WARN("SE050: response of %u bytes does not fit in %u", (unsigned)(3 + body), (unsigned)rxCap);
        return 0;
    }
    if (body > 0) {
        if (bus.requestFrom(address, body) != body) {
            LOG_DEBUG("SE050: body read failed (LEN=%u)", (unsigned)header[2]);
            return 0;
        }
        for (size_t i = 0; i < body; i++)
            rx[3 + i] = bus.read();
    }

    // Validate the CRC so a desynchronised read is discarded rather than parsed.
    // Retransmitting resynchronises the SE050, so the caller can simply retry.
    size_t total = 3 + body;
    if (total >= 5) {
        uint16_t want = crc(rx, total - 2);
        if (rx[total - 2] != ((want >> 8) & 0xFF) || rx[total - 1] != (want & 0xFF)) {
            LOG_DEBUG("SE050: response CRC mismatch, discarding frame");
            return 0;
        }
    }
    return total;
}

bool SE050::reset(uint8_t *atrOut, size_t atrCap, size_t *atrLen)
{
    uint8_t frame[5] = {NAD_HOST_TO_SE, (uint8_t)(PCB_S_REQ | S_INTF_RESET), 0x00, 0, 0};
    uint16_t c = crc(frame, 3);
    frame[3] = (c >> 8) & 0xFF;
    frame[4] = c & 0xFF;

    const uint8_t expected = (uint8_t)(PCB_S_RSP | S_INTF_RESET); // 0xEF

    // Retransmitting the interface reset is also how a stream that went out of
    // step is recovered, so a failed attempt is worth repeating.
    for (int attempt = 0; attempt < 4; attempt++) {
        uint8_t rx[128];
        size_t n = xfer(frame, sizeof(frame), rx, sizeof(rx));
        if (n >= 3 && rx[1] == expected) {
            size_t len = rx[2];
            if (3 + len > n)
                len = n - 3;
            if (atrOut && atrLen) {
                size_t copy = len < atrCap ? len : atrCap;
                memcpy(atrOut, &rx[3], copy);
                *atrLen = copy;
            }
            seq = 0;
            return true;
        }
        LOG_DEBUG("SE050: interface reset attempt %d gave %s", attempt + 1, n == 0 ? "no valid frame" : "an unexpected PCB");
    }
    return false;
}

uint16_t SE050::statusWord(const uint8_t *resp, int len)
{
    return len >= 2 ? (uint16_t)((resp[len - 2] << 8) | resp[len - 1]) : 0xFFFF;
}

int SE050::transceive(const uint8_t *apdu, size_t apduLen, uint8_t *resp, size_t respCap)
{
    if (reentered("transceive"))
        return -1;

    uint8_t *const frame = txFrame;
    // LEN (below) is a single byte (UM11225), tighter than the txFrame capacity check alone -
    // without this, 256-283 bytes would pass the capacity check and then silently wrap into a
    // wrong LEN.
    if (apduLen > 255 || 5 + apduLen > sizeof(txFrame))
        return -1;

    frame[0] = NAD_HOST_TO_SE;
    frame[1] = (uint8_t)((seq & 1) << 6); // I-block: bit7=0, N(S) in bit6
    frame[2] = (uint8_t)apduLen;          // LEN, single byte (UM11225)
    memcpy(&frame[3], apdu, apduLen);
    uint16_t c = crc(frame, 3 + apduLen);
    frame[3 + apduLen] = (c >> 8) & 0xFF;
    frame[4 + apduLen] = c & 0xFF;

    uint8_t *const rx = rxFrame;
    size_t n = xfer(frame, 5 + apduLen, rx, sizeof(rxFrame));
    seq ^= 1;
    if (n == 0)
        return -1;

    // WTX: the SE050 asks for more time (S-block request). Grant it and re-read.
    //
    // Bounded, because a chip that keeps asking would otherwise spin here forever,
    // and xfer feeds the watchdog on every pass - so the board would hang silently
    // rather than reset. Each grant already allows a full poll window, so twenty of
    // them is far more patience than any real operation needs.
    for (int grants = 0; n >= 2 && rx[1] == (uint8_t)(PCB_S_REQ | S_WTX); grants++) {
        if (grants >= MAX_WTX_GRANTS) {
            LOG_ERROR("SE050: chip kept asking for more time, giving up");
            return -1;
        }
        uint8_t wtx = rx[2] >= 1 ? rx[3] : 1;
        uint8_t w[6] = {NAD_HOST_TO_SE, (uint8_t)(PCB_S_RSP | S_WTX), 0x01, wtx, 0, 0};
        uint16_t wc = crc(w, 4);
        w[4] = (wc >> 8) & 0xFF;
        w[5] = wc & 0xFF;
        n = xfer(w, sizeof(w), rx, sizeof(rxFrame));
        if (n == 0)
            return -1;
    }

    size_t len = rx[2];
    if (3 + len > n)
        len = n > 3 ? n - 3 : 0;
    if (len > respCap)
        len = respCap;
    memcpy(resp, &rx[3], len);
    return (int)len;
}

bool SE050::selectApplet()
{
    // SELECT (by name) the SE05x IoT applet - AID A0000003965453000000010300000000.
    static const uint8_t SEL[] = {0x00, 0xA4, 0x04, 0x00, 0x10, 0xA0, 0x00, 0x00, 0x03, 0x96, 0x54,
                                  0x53, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
    uint8_t r[64];
    int n = transceive(SEL, sizeof(SEL), r, sizeof(r));
    uint16_t sw = statusWord(r, n);
    if (sw != 0x9000) {
        LOG_ERROR("SE050: applet SELECT returned SW=%04x", sw);
        return false;
    }
    if (n >= 3)
        LOG_INFO("SE050: IoT applet selected, version %d.%d.%d", r[0], r[1], r[2]);
    return true;
}

bool SE050::open()
{
    if (!reset())
        return false;
    return selectApplet();
}

// --- PlatformSCP03 -------------------------------------------------------
//
// Factory keys for OEF 0x0001A921 (SE050E). These are the default keys NXP ships
// in its middleware, so they are public and confer no secrecy - they only open
// the transport channel the SE050 requires before it will do key agreement.
static const uint8_t SCP_KEY_ENC[16] = {0xD2, 0xDB, 0x63, 0xE7, 0xA0, 0xA5, 0xAE, 0xD7,
                                        0x2A, 0x64, 0x60, 0xC4, 0xDF, 0xDC, 0xAF, 0x64};
static const uint8_t SCP_KEY_MAC[16] = {0x73, 0x8D, 0x5B, 0x79, 0x8E, 0xD2, 0x41, 0xB0,
                                        0xB2, 0x47, 0x68, 0x51, 0x4B, 0xFB, 0xA9, 0x5B};
static constexpr uint8_t SCP03_KEYVER = 0x0B;

// AES-CMAC (RFC 4493). The bundled Crypto library only exposes OMAC in its EAX
// form, which prepends a tag block, so the plain construction is done here.
void SE050::cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t out[16])
{
    AES128 aes;
    aes.setKey(key, 16);

    // Subkeys: L = E(K, 0), K1 = dbl(L), K2 = dbl(K1); dbl left-shifts and, on
    // carry out of the top bit, folds in the 0x87 field polynomial.
    uint8_t k1[16] = {0}, k2[16] = {0};
    aes.encryptBlock(k1, k1);
    for (int round = 0; round < 2; round++) {
        uint8_t *k = round == 0 ? k1 : k2;
        if (round == 1)
            memcpy(k2, k1, 16);
        uint8_t carry = k[0] & 0x80;
        for (int i = 0; i < 15; i++)
            k[i] = (uint8_t)((k[i] << 1) | (k[i + 1] >> 7));
        k[15] = (uint8_t)(k[15] << 1);
        if (carry)
            k[15] ^= 0x87;
    }

    uint8_t state[16] = {0};
    size_t full = len ? (len - 1) / 16 : 0; // blocks processed before the last one
    for (size_t b = 0; b < full; b++) {
        for (int i = 0; i < 16; i++)
            state[i] ^= data[b * 16 + i];
        aes.encryptBlock(state, state);
    }

    // Last block: XOR K1 if it is exactly full, otherwise pad with 0x80 00.. and
    // XOR K2 instead.
    uint8_t last[16] = {0};
    size_t rem = len - full * 16;
    if (len > 0 && rem == 16) {
        memcpy(last, &data[full * 16], 16);
        for (int i = 0; i < 16; i++)
            last[i] ^= k1[i];
    } else {
        memcpy(last, &data[full * 16], rem);
        last[rem] = 0x80;
        for (int i = 0; i < 16; i++)
            last[i] ^= k2[i];
    }
    for (int i = 0; i < 16; i++)
        state[i] ^= last[i];
    aes.encryptBlock(out, state);
}

// SCP03 key derivation (SP800-108 in counter mode, CMAC as the PRF).
void SE050::kdf(const uint8_t key[16], uint8_t constant, uint16_t bits, const uint8_t context[16], uint8_t out[16])
{
    uint8_t dd[32];
    memset(dd, 0, 11);
    dd[11] = constant;
    dd[12] = 0x00;
    dd[13] = (uint8_t)(bits >> 8);
    dd[14] = (uint8_t)(bits & 0xFF);
    dd[15] = 0x01;
    memcpy(&dd[16], context, 16);
    cmac(key, dd, sizeof(dd), out);
}

void SE050::sessionKeys(const uint8_t context[16])
{
    kdf(SCP_KEY_ENC, 0x04, 128, context, scp.senc);
    kdf(SCP_KEY_MAC, 0x06, 128, context, scp.smac);
    kdf(SCP_KEY_MAC, 0x07, 128, context, scp.srmac);
}

// Cryptograms are the first 8 bytes of a 64-bit derivation off S-MAC.
void SE050::cryptogram(uint8_t constant, const uint8_t context[16], uint8_t out[8])
{
    uint8_t full[16];
    kdf(scp.smac, constant, 64, context, full);
    memcpy(out, full, 8);
}

// C-MAC over MCV || command, updating the MCV to the full CMAC so successive
// commands chain.
void SE050::chainedCmac(const uint8_t *cmd, size_t len, uint8_t mac[8])
{
    uint8_t buf[16 + 288];
    if (16 + len > sizeof(buf))
        return;
    memcpy(buf, scp.mcv, 16);
    memcpy(&buf[16], cmd, len);
    uint8_t full[16];
    cmac(scp.smac, buf, 16 + len, full);
    memcpy(scp.mcv, full, 16);
    memcpy(mac, full, 8);
}

bool SE050::initializeUpdate(const uint8_t hostChallenge[8], uint8_t cardChallenge[8], uint8_t cardCryptogram[8])
{
    uint8_t iu[] = {0x80,
                    0x50,
                    SCP03_KEYVER,
                    0x00,
                    0x08,
                    hostChallenge[0],
                    hostChallenge[1],
                    hostChallenge[2],
                    hostChallenge[3],
                    hostChallenge[4],
                    hostChallenge[5],
                    hostChallenge[6],
                    hostChallenge[7],
                    0x00};
    uint8_t r[64];
    int n = transceive(iu, sizeof(iu), r, sizeof(r));
    if (statusWord(r, n) != 0x9000 || n < 31) {
        LOG_ERROR("SE050: INITIALIZE UPDATE (keyver=%02x) SW=%04x n=%d", SCP03_KEYVER, statusWord(r, n), n);
        return false;
    }
    // keyDivData(10) || keyInfo(3) || cardChallenge(8) || cardCryptogram(8)
    memcpy(cardChallenge, &r[13], 8);
    memcpy(cardCryptogram, &r[21], 8);
    return true;
}

bool SE050::openSecureChannel()
{
    memset(&scp, 0, sizeof(scp));

    uint8_t hostChallenge[8];
    if (!HardwareRNG::fill(hostChallenge, sizeof(hostChallenge))) {
        LOG_ERROR("SE050: no entropy source available for the SCP03 host challenge");
        return false;
    }

    uint8_t cardChallenge[8], cardCryptogram[8];
    if (!initializeUpdate(hostChallenge, cardChallenge, cardCryptogram))
        return false;

    uint8_t context[16];
    memcpy(context, hostChallenge, 8);
    memcpy(&context[8], cardChallenge, 8);
    sessionKeys(context);

    // If this does not match, the static keys or the KDF are wrong - there is no
    // point continuing, and it is also how the chip authenticates itself to us.
    uint8_t expected[8];
    cryptogram(0x00, context, expected);
    if (memcmp(expected, cardCryptogram, 8) != 0) {
        LOG_ERROR("SE050: card cryptogram mismatch - keys rotated, or a different KDF");
        return false;
    }

    uint8_t hostCryptogram[8];
    cryptogram(0x01, context, hostCryptogram);

    // EXTERNAL AUTHENTICATE. CLA 0x84 carries the security bit; P1 0x33 asks for
    // C-DEC | C-MAC | R-MAC | R-ENC. Lc covers the cryptogram plus its C-MAC, and
    // the C-MAC chains from the still-zero MCV.
    uint8_t cmd[13];
    cmd[0] = 0x84;
    cmd[1] = 0x82;
    cmd[2] = 0x33;
    cmd[3] = 0x00;
    cmd[4] = 0x10;
    memcpy(&cmd[5], hostCryptogram, 8);

    uint8_t mac[8];
    chainedCmac(cmd, sizeof(cmd), mac);

    uint8_t apdu[sizeof(cmd) + 8];
    memcpy(apdu, cmd, sizeof(cmd));
    memcpy(&apdu[sizeof(cmd)], mac, 8);

    uint8_t r[32];
    int n = transceive(apdu, sizeof(apdu), r, sizeof(r));
    uint16_t sw = statusWord(r, n);
    if (sw != 0x9000) {
        LOG_ERROR("SE050: EXTERNAL AUTHENTICATE SW=%04x - channel not open", sw);
        return false;
    }

    scp.open = true;
    scp.counter = 0; // the first wrapped command increments this to 1
    return true;
}

// --- Secure channel wrapping ---------------------------------------------

void SE050::cbc(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out, bool encrypt)
{
    AES128 aes;
    aes.setKey(key, 16);
    uint8_t chain[16];
    memcpy(chain, iv, 16);
    for (size_t off = 0; off < len; off += 16) {
        if (encrypt) {
            uint8_t block[16];
            for (int i = 0; i < 16; i++)
                block[i] = in[off + i] ^ chain[i];
            aes.encryptBlock(&out[off], block);
            memcpy(chain, &out[off], 16);
        } else {
            uint8_t cipher[16];
            memcpy(cipher, &in[off], 16);
            aes.decryptBlock(&out[off], cipher);
            for (int i = 0; i < 16; i++)
                out[off + i] ^= chain[i];
            memcpy(chain, cipher, 16);
        }
    }
}

// The encryption IV is AES-ECB(S-ENC, counter block). The counter goes in the low
// bytes big-endian; for a response the top byte of the block is 0x80.
void SE050::encryptionIcv(bool response, uint8_t icv[16])
{
    uint8_t blk[16] = {0};
    blk[12] = (uint8_t)(scp.counter >> 24);
    blk[13] = (uint8_t)(scp.counter >> 16);
    blk[14] = (uint8_t)(scp.counter >> 8);
    blk[15] = (uint8_t)(scp.counter);
    if (response)
        blk[0] = 0x80;
    AES128 aes;
    aes.setKey(scp.senc, 16);
    aes.encryptBlock(icv, blk);
}

int SE050::secureApdu(const uint8_t header[4], const uint8_t *data, int dataLen, bool expectResponse, uint8_t *resp, int respCap,
                      uint16_t *sw)
{
    if (!scp.open || reentered("secureApdu")) {
        *sw = 0xFFFF;
        return -1;
    }

    scp.counter++;

    uint8_t *const enc = encBuf;
    int encLen = 0;
    if (dataLen > 0) {
        uint8_t *const padded = padBuf;
        if ((size_t)dataLen + 16 > sizeof(padBuf)) {
            // scp.counter already advanced for a command that never reached the card, so the
            // host and card ICVs/MCVs can no longer agree - closing the channel here (and at
            // every other exit below that can't confirm the card is still in step) forces the
            // next call to reopen instead of building C-MACs the card will reject forever.
            scp.open = sessionActive = false;
            *sw = 0xFFFF;
            return -1;
        }
        memcpy(padded, data, dataLen);
        padded[dataLen] = 0x80; // SCP03 pads with 80 00 .. to the block size
        encLen = ((dataLen + 1 + 15) / 16) * 16;
        if (encLen + 8 > 255) {
            // Lc (below) is a single ISO7816 short-form byte, tighter than what padBuf alone
            // allows - without this, a payload in the ~240-283 byte range would silently wrap
            // Lc instead of getting rejected here.
            scp.open = sessionActive = false;
            *sw = 0xFFFF;
            return -1;
        }
        memset(&padded[dataLen + 1], 0, encLen - (dataLen + 1));
        uint8_t icv[16];
        encryptionIcv(false, icv);
        cbc(scp.senc, icv, padded, encLen, enc, true);
    }

    uint8_t *const out = apduOut;
    int p = 0;
    out[p++] = (uint8_t)(header[0] | 0x04); // CLA carries the security bit
    out[p++] = header[1];
    out[p++] = header[2];
    out[p++] = header[3];
    out[p++] = (uint8_t)(encLen + 8); // Lc counts the C-MAC too
    memcpy(&out[p], enc, encLen);
    p += encLen;

    uint8_t mac[8];
    chainedCmac(out, p, mac);
    memcpy(&out[p], mac, 8);
    p += 8;
    if (expectResponse)
        out[p++] = 0x00;

    uint8_t *const r = apduIn;
    int n = transceive(out, p, r, sizeof(apduIn));
    if (n < 0) {
        scp.open = sessionActive = false;
        *sw = 0xFFFF;
        return -1;
    }

    // Response is [encrypted data][R-MAC 8][SW 2]. A short frame is the card answering with a
    // bare status word and no R-MAC, which is what it does for an ordinary application error:
    // the 6985 from the idempotent setup steps in identitySession() is one, and so is the failed
    // ReadObject that tells identityEnsure() there is no identity on the chip yet. Both of those
    // commands did travel the secure channel - the card accepted the C-MAC and advanced its
    // counter in step with ours - so the channel is still good and the caller only needs the SW.
    // Closing here regardless made every idempotent step poison the session it was preparing.
    //
    // Only a response with no status word at all, or one saying the secure messaging itself was
    // refused, can leave the two sides disagreeing about the counter.
    if (n < 10) {
        *sw = statusWord(r, n);
        if (n < 2 || *sw == 0x6982 || *sw == 0x6987 || *sw == 0x6988) {
            LOG_WARN("SE050: secure channel closed after a %04x response, it will be reopened", *sw);
            scp.open = sessionActive = false;
        }
        return n >= 2 ? 0 : -1;
    }
    *sw = (uint16_t)((r[n - 2] << 8) | r[n - 1]);

    int encRespLen = n - 10;
    uint8_t *const buf = macBuf;
    if ((size_t)(16 + encRespLen + 2) > sizeof(macBuf)) {
        LOG_ERROR("SE050: response of %d bytes is too long to verify", encRespLen);
        scp.open = sessionActive = false;
        return -1;
    }
    memcpy(buf, scp.mcv, 16); // R-MAC reads the MCV but must not advance it
    memcpy(&buf[16], r, encRespLen);
    buf[16 + encRespLen] = r[n - 2];
    buf[16 + encRespLen + 1] = r[n - 1];
    uint8_t full[16];
    cmac(scp.srmac, buf, 16 + encRespLen + 2, full);
    if (memcmp(full, &r[n - 10], 8) != 0) {
        LOG_ERROR("SE050: R-MAC verification failed");
        scp.open = sessionActive = false;
        return -1;
    }
    if (encRespLen == 0)
        return 0;

    uint8_t icv[16];
    encryptionIcv(true, icv);
    uint8_t *const plain = plainBuf;
    if ((size_t)encRespLen > sizeof(plainBuf))
        return -1;
    cbc(scp.senc, icv, r, encRespLen, plain, false);

    int len = encRespLen; // strip the 80 00 .. padding
    while (len > 0 && plain[len - 1] == 0x00)
        len--;
    if (len > 0 && plain[len - 1] == 0x80)
        len--;
    if (len > respCap)
        len = respCap;
    memcpy(resp, plain, len);
    return len;
}

int SE050::sessionApdu(const uint8_t header[4], const uint8_t *data, int dataLen, bool expectResponse, uint8_t *resp, int respCap,
                       uint16_t *sw)
{
    if (reentered("sessionApdu")) {
        *sw = 0xFFFF;
        return -1;
    }

    uint8_t *const od = sessionBuf;
    int j = 0;
    int innerLc = (dataLen == 0) ? 0 : ((dataLen < 0xFF && !expectResponse) ? 1 : 3);
    int tagLen = 4 + innerLc + dataLen;
    if ((size_t)(14 + tagLen) > sizeof(sessionBuf)) { // session id TLV, TAG_1 header, inner command
        *sw = 0xFFFF;
        return -1;
    }

    od[j++] = 0x10; // TAG_SESSION_ID
    od[j++] = 0x08;
    memcpy(&od[j], sessionId, 8);
    j += 8;
    od[j++] = 0x41; // TAG_1 wraps the inner command
    if (tagLen <= 0x7F) {
        od[j++] = (uint8_t)tagLen;
    } else if (tagLen <= 0xFF) {
        od[j++] = 0x81;
        od[j++] = (uint8_t)tagLen;
    } else {
        od[j++] = 0x82;
        od[j++] = (uint8_t)(tagLen >> 8);
        od[j++] = (uint8_t)tagLen;
    }
    memcpy(&od[j], header, 4);
    j += 4;
    if (dataLen > 0) {
        if (dataLen < 0xFF && !expectResponse) {
            od[j++] = (uint8_t)dataLen;
        } else {
            od[j++] = 0x00;
            od[j++] = (uint8_t)(dataLen >> 8);
            od[j++] = (uint8_t)dataLen;
        }
        memcpy(&od[j], data, dataLen);
        j += dataLen;
    }

    static const uint8_t PROCESS_SESSION_CMD[4] = {0x80, 0x05, 0x00, 0x00};
    return secureApdu(PROCESS_SESSION_CMD, od, j, expectResponse, resp, respCap, sw);
}

// First TLV with tag 0x41, handling BER short and long form lengths.
const uint8_t *SE050::tlv1(const uint8_t *resp, int len, int *valueLen)
{
    *valueLen = 0;
    if (len < 2 || resp[0] != 0x41)
        return nullptr;
    int off, l;
    if (resp[1] == 0x82) {
        l = (resp[2] << 8) | resp[3];
        off = 4;
    } else if (resp[1] == 0x81) {
        l = resp[2];
        off = 3;
    } else {
        l = resp[1];
        off = 2;
    }
    if (off + l > len)
        l = len - off;
    *valueLen = l;
    return &resp[off];
}

void SE050::reverse(const uint8_t *in, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++)
        out[i] = in[len - 1 - i];
}

// --- Identity ------------------------------------------------------------

namespace
{
constexpr uint32_t IDENTITY_OBJ = 0x4D544944u; // "MTID", the node's X25519 identity
// "MTKY", the mirrored copy of the key Meshtastic already holds in its config.
// Deliberately a different object from MTID so the chip-generated identity, and
// the self-test that leans on it, stay intact.
constexpr uint32_t NODE_KEY_OBJ = 0x4D544B59u;
constexpr uint32_t AUTH_OBJ = 0x20000AAAu; // UserID authenticator the key is bound to
const uint8_t AUTH_PIN[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

void be32(uint32_t v, uint8_t out[4])
{
    out[0] = (uint8_t)(v >> 24);
    out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);
    out[3] = (uint8_t)v;
}
} // namespace

// Curve, authenticator and UserID session: everything an identity object needs
// before it can be created, read or used. Every step is idempotent, so both the
// generate and the import path open with this.
bool SE050::identitySession()
{
    if (!scp.open) {
        LOG_ERROR("SE050: identity needs an open secure channel");
        return false;
    }
    // A session opened earlier is still good: nothing closes it, and asking the
    // chip for a second one while the first is live is not a request it expects.
    if (sessionActive)
        return true;

    uint8_t authId[4];
    be32(AUTH_OBJ, authId);

    uint8_t r[192];
    uint16_t sw = 0;
    int rl, vl;
    const uint8_t *v;

    // Applet 7.x does not ship the Montgomery curve pre-created, and key agreement
    // against an external public key by byte-array is the one path that needs it.
    // Idempotent: 6985 just means it already exists.
    {
        const uint8_t h[4] = {0x80, 0x01, 0x0B, 0x04};
        uint8_t d[] = {0x41, 0x01, 0x41};
        secureApdu(h, d, sizeof(d), false, r, sizeof(r), &sw);
        if (sw != 0x9000 && sw != 0x6985)
            LOG_WARN("SE050: CreateECCurve SW=%04x", sw);
    }

    // UserID authenticator. INS carries the AUTH_OBJECT bit (0x40). Also idempotent.
    {
        const uint8_t h[4] = {0x80, 0x41, 0x07, 0x00};
        uint8_t d[] = {0x41,        0x04,        authId[0],   authId[1],   authId[2],   authId[3],   0x42,        0x08,
                       AUTH_PIN[0], AUTH_PIN[1], AUTH_PIN[2], AUTH_PIN[3], AUTH_PIN[4], AUTH_PIN[5], AUTH_PIN[6], AUTH_PIN[7]};
        secureApdu(h, d, sizeof(d), false, r, sizeof(r), &sw);
        if (sw != 0x9000 && sw != 0x6985)
            LOG_WARN("SE050: WriteUserID SW=%04x", sw);
    }

    // Open and authenticate a UserID session nested inside the secure channel.
    {
        const uint8_t h[4] = {0x80, 0x04, 0x00, 0x1B};
        uint8_t d[] = {0x41, 0x04, authId[0], authId[1], authId[2], authId[3]};
        rl = secureApdu(h, d, sizeof(d), true, r, sizeof(r), &sw);
        if (sw != 0x9000) {
            LOG_ERROR("SE050: CreateSession SW=%04x", sw);
            return false;
        }
        v = tlv1(r, rl, &vl);
        if (!v || vl != 8) {
            LOG_ERROR("SE050: unexpected session id");
            return false;
        }
        memcpy(sessionId, v, 8);
    }
    {
        const uint8_t h[4] = {0x80, 0x04, 0x00, 0x2C};
        uint8_t d[] = {0x41,        0x08,        AUTH_PIN[0], AUTH_PIN[1], AUTH_PIN[2],
                       AUTH_PIN[3], AUTH_PIN[4], AUTH_PIN[5], AUTH_PIN[6], AUTH_PIN[7]};
        sessionApdu(h, d, sizeof(d), false, r, sizeof(r), &sw);
        if (sw != 0x9000) {
            LOG_ERROR("SE050: VerifySessionUserID SW=%04x", sw);
            return false;
        }
    }
    sessionActive = true;
    return true;
}

bool SE050::identityEnsure(uint8_t publicKey[32])
{
    identityReady = false;
    if (!identitySession())
        return false;

    uint8_t authId[4], keyId[4];
    be32(AUTH_OBJ, authId);
    be32(IDENTITY_OBJ, keyId);

    uint8_t r[192];
    uint16_t sw = 0;
    int rl, vl;
    const uint8_t *v;

    // Read the identity; if it is not there, generate it. Generation is persistent,
    // so this only ever happens once per chip.
    {
        const uint8_t hRead[4] = {0x80, 0x02, 0x00, 0x00};
        uint8_t dRead[] = {0x41, 0x04, keyId[0], keyId[1], keyId[2], keyId[3]};
        rl = sessionApdu(hRead, dRead, sizeof(dRead), true, r, sizeof(r), &sw);
        if (sw != 0x9000) {
            LOG_INFO("SE050: no identity yet, generating X25519 in-chip (objId 0x%08x)", (unsigned)IDENTITY_OBJ);
            const uint8_t hGen[4] = {0x80, 0x01, 0x61, 0x00};
            // Policy: bound to the UserID authenticator, allowing key agreement.
            uint8_t dGen[] = {0x11, 0x09, 0x08, authId[0], authId[1], authId[2], authId[3], 0x04, 0x3C, 0x00,
                              0x00, 0x41, 0x04, keyId[0],  keyId[1],  keyId[2],  keyId[3],  0x42, 0x01, 0x41};
            sessionApdu(hGen, dGen, sizeof(dGen), false, r, sizeof(r), &sw);
            if (sw != 0x9000) {
                LOG_ERROR("SE050: WriteECKey SW=%04x", sw);
                return false;
            }
            rl = sessionApdu(hRead, dRead, sizeof(dRead), true, r, sizeof(r), &sw);
            if (sw != 0x9000) {
                LOG_ERROR("SE050: ReadObject SW=%04x", sw);
                return false;
            }
        } else {
            LOG_INFO("SE050: reusing existing identity (objId 0x%08x)", (unsigned)IDENTITY_OBJ);
        }
        v = tlv1(r, rl, &vl);
        if (!v || vl < 32) {
            LOG_ERROR("SE050: unexpected public key length %d", vl);
            return false;
        }
        reverse(v, publicKey, 32); // the SE050 reports big-endian
    }

    activeKeyObj = IDENTITY_OBJ;
    identityReady = true;
    return true;
}

bool SE050::identityImport(const uint8_t privateKey[32], uint8_t publicKeyOut[32], bool replaceStale)
{
    identityReady = false;
    if (!identitySession())
        return false;

    uint8_t authId[4], keyId[4];
    be32(AUTH_OBJ, authId);
    be32(NODE_KEY_OBJ, keyId);

    // WriteECKey wants both halves of a key pair or neither, and Meshtastic only
    // keeps the private one, so derive the public half here.
    uint8_t privLe[32], pubLe[32];
    memcpy(privLe, privateKey, 32);
    Curve25519::eval(pubLe, privLe, 0);
    memcpy(publicKeyOut, pubLe, 32);

    uint8_t r[192];
    uint16_t sw = 0;
    int rl, vl;
    const uint8_t *v;

    // Already there? Compare before writing. This is what keeps the NVM write to
    // once per node instead of once per boot.
    const uint8_t hRead[4] = {0x80, 0x02, 0x00, 0x00};
    uint8_t dRead[] = {0x41, 0x04, keyId[0], keyId[1], keyId[2], keyId[3]};
    rl = sessionApdu(hRead, dRead, sizeof(dRead), true, r, sizeof(r), &sw);
    if (sw == 0x9000) {
        v = tlv1(r, rl, &vl);
        uint8_t onChip[32];
        if (v && vl >= 32) {
            reverse(v, onChip, 32); // the SE050 reports big-endian
            if (memcmp(onChip, pubLe, 32) == 0) {
                LOG_INFO("SE050: node key already mirrored (objId 0x%08x)", (unsigned)NODE_KEY_OBJ);
                activeKeyObj = NODE_KEY_OBJ;
                identityReady = true;
                return true;
            }
        }
        if (!replaceStale) {
            LOG_WARN("SE050: objId 0x%08x holds a different key - refusing to overwrite an identity", (unsigned)NODE_KEY_OBJ);
            LOG_WARN("SE050: build with -D SE050_REPLACE_MIRROR once to discard it and mirror the current node key");
            return false;
        }

        // Asked for explicitly, and only then. The stale object is a mirror of a
        // node key that no longer exists, so nothing is lost with it - but that
        // is a judgement about this object in this stage of the port, not one
        // the driver gets to make on its own for any key it finds in the way.
        LOG_WARN("SE050: objId 0x%08x holds a different key - replacing it as asked", (unsigned)NODE_KEY_OBJ);
        const uint8_t hDelete[4] = {0x80, 0x04, 0x00, 0x28};
        uint8_t dDelete[] = {0x41, 0x04, keyId[0], keyId[1], keyId[2], keyId[3]};
        sessionApdu(hDelete, dDelete, sizeof(dDelete), false, r, sizeof(r), &sw);
        if (sw != 0x9000) {
            // 6985 here means the object's own policy does not carry ALLOW_DELETE,
            // in which case it cannot be removed through this session at all.
            LOG_ERROR("SE050: DeleteSecureObject SW=%04x, the stale mirror stays", sw);
            return false;
        }
    }

    // Montgomery keys go in big-endian, private half included (AN12413 section 7.2).
    uint8_t privBe[32], pubBe[32];
    reverse(privLe, privBe, 32);
    reverse(pubLe, pubBe, 32);

    LOG_INFO("SE050: mirroring node key into the chip (objId 0x%08x)", (unsigned)NODE_KEY_OBJ);
    uint8_t d[128];
    int j = 0;
    // Policy: bound to the UserID authenticator, allowing key agreement.
    d[j++] = 0x11;
    d[j++] = 0x09;
    d[j++] = 0x08;
    memcpy(&d[j], authId, 4);
    j += 4;
    d[j++] = 0x04;
    d[j++] = 0x3C;
    d[j++] = 0x00;
    d[j++] = 0x00;
    d[j++] = 0x41; // TAG_1: object id
    d[j++] = 0x04;
    memcpy(&d[j], keyId, 4);
    j += 4;
    d[j++] = 0x42; // TAG_2: curve
    d[j++] = 0x01;
    d[j++] = 0x41;
    d[j++] = 0x43; // TAG_3: private half
    d[j++] = 0x20;
    memcpy(&d[j], privBe, 32);
    j += 32;
    d[j++] = 0x44; // TAG_4: public half
    d[j++] = 0x20;
    memcpy(&d[j], pubBe, 32);
    j += 32;

    const uint8_t hWrite[4] = {0x80, 0x01, 0x61, 0x00}; // P1_EC | P1_KEY_PAIR
    sessionApdu(hWrite, d, j, false, r, sizeof(r), &sw);
    if (sw != 0x9000) {
        LOG_ERROR("SE050: WriteECKey (import) SW=%04x", sw);
        return false;
    }

    // Read it back: proves the byte order was right rather than assuming it.
    rl = sessionApdu(hRead, dRead, sizeof(dRead), true, r, sizeof(r), &sw);
    if (sw != 0x9000) {
        LOG_ERROR("SE050: ReadObject after import SW=%04x", sw);
        return false;
    }
    v = tlv1(r, rl, &vl);
    uint8_t readBack[32];
    if (!v || vl < 32) {
        LOG_ERROR("SE050: unexpected public key length %d after import", vl);
        return false;
    }
    reverse(v, readBack, 32);
    if (memcmp(readBack, pubLe, 32) != 0) {
        LOG_ERROR("SE050: imported key does not read back - byte order is wrong");
        return false;
    }

    activeKeyObj = NODE_KEY_OBJ;
    identityReady = true;
    return true;
}

bool SE050::identityEcdh(const uint8_t peerPublic[32], uint8_t shared[32])
{
    if (!identityReady || reentered("identityEcdh"))
        return false;

    uint8_t keyId[4];
    be32(activeKeyObj, keyId);
    uint8_t peerBe[32];
    reverse(peerPublic, peerBe, 32);

    const uint8_t h[4] = {0x80, 0x03, 0x01, 0x0F}; // INS_CRYPTO, P1_EC, P2_DH
    uint8_t d[40];
    int j = 0;
    d[j++] = 0x41; // TAG_1: the on-chip private key
    d[j++] = 0x04;
    memcpy(&d[j], keyId, 4);
    j += 4;
    d[j++] = 0x42; // TAG_2: peer public key, big-endian
    d[j++] = 0x20;
    memcpy(&d[j], peerBe, 32);
    j += 32;

    uint8_t r[128];
    uint16_t sw = 0;
    int rl = sessionApdu(h, d, j, true, r, sizeof(r), &sw);
    if (sw != 0x9000) {
        LOG_ERROR("SE050: ECDH SW=%04x", sw);
        return false;
    }
    int vl;
    const uint8_t *v = tlv1(r, rl, &vl);
    if (!v || vl != 32)
        return false;
    reverse(v, shared, 32);
    return true;
}

bool SE050::probe()
{
    if (!open()) {
        LOG_ERROR("SE050: bring-up failed at 0x%x", address);
        return false;
    }

    // GetVersion: CLA=80 INS_MGMT=04 P1=00 P2_VERSION=20, Le=00. The 7-byte
    // VersionInfo comes back in a BER-TLV (tag 0x41): applet version, then the
    // 2-byte AppletConfig and 2-byte SecureBox. Long-form length is possible.
    static const uint8_t GV[] = {0x80, 0x04, 0x00, 0x20, 0x00};
    uint8_t r[32];
    int n = transceive(GV, sizeof(GV), r, sizeof(r));
    if (statusWord(r, n) == 0x9000) {
        const uint8_t *vi = r;
        int off = 0;
        if (n >= 2 && r[0] == 0x41) {
            if (r[1] == 0x82)
                off = 4;
            else if (r[1] == 0x81)
                off = 3;
            else
                off = 2;
        }
        vi = &r[off];
        if (n - off >= 7) {
            uint16_t cfg = (uint16_t)((vi[3] << 8) | vi[4]);
            LOG_INFO("SE050: applet %d.%d.%d AppletConfig=0x%04x (DH_MONT %s, FIPS %s)", vi[0], vi[1], vi[2], cfg,
                     (cfg & 0x0008) ? "on" : "off", (cfg & 0x1000) ? "off" : "on");
        }
    } else {
        LOG_WARN("SE050: GetVersion returned SW=%04x", statusWord(r, n));
    }

    // GetRandom (16 bytes): CLA=80 INS_MGMT=04 P1=00 P2_RANDOM=49, TLV 41 02 <size>, Le=00.
    // Proves the on-chip TRNG is live - the bytes must differ every boot.
    static const uint8_t GR[] = {0x80, 0x04, 0x00, 0x49, 0x04, 0x41, 0x02, 0x00, 0x10, 0x00};
    n = transceive(GR, sizeof(GR), r, sizeof(r));
    if (statusWord(r, n) == 0x9000 && n >= 2 && r[0] == 0x41) {
        int off = (r[1] == 0x82) ? 4 : (r[1] == 0x81) ? 3 : 2;
        int rl = (r[1] == 0x82) ? ((r[2] << 8) | r[3]) : (r[1] == 0x81) ? r[2] : r[1];
        char hex[2 * 16 + 1];
        int shown = 0;
        for (int i = 0; i < rl && off + i < n - 2 && shown < 16; i++, shown++)
            snprintf(&hex[shown * 2], 3, "%02X", r[off + i]);
        hex[shown * 2] = '\0';
        LOG_INFO("SE050: GetRandom OK, TRNG live: %s", hex);
    } else {
        LOG_WARN("SE050: GetRandom returned SW=%04x", statusWord(r, n));
    }

    if (!openSecureChannel()) {
        LOG_WARN("SE050: SCP03 secure channel not established");
        return true;
    }
    // "Open", not "authenticated": the keys are NXP's public factory defaults (see
    // SCP_KEY_ENC/MAC above), so this confirms a chip that speaks PlatformSCP03
    // correctly, not one we trust more than an attacker with physical I2C access
    // could. Rotating to per-device keys is a prerequisite for that claim, not done here.
    LOG_INFO("SE050: SCP03 secure channel open");

    uint8_t ourPublic[32];
    if (!identityEnsure(ourPublic)) {
        LOG_WARN("SE050: identity not available");
        return true;
    }
    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(&hex[i * 2], 3, "%02X", ourPublic[i]);
    LOG_INFO("SE050: identity public key %s", hex);

    // Equivalence check: generate a throwaway keypair in software, do the exchange
    // both ways, and compare. If the SE050 agrees with Curve25519 byte for byte,
    // the whole chain - byte order, curve, policy, session - is correct, and the
    // chip can stand in for the software implementation.
    uint8_t testPrivate[32], testPublic[32];
    Curve25519::dh1(testPublic, testPrivate);

    uint8_t sharedChip[32];
    if (!identityEcdh(testPublic, sharedChip)) {
        LOG_WARN("SE050: ECDH failed, cannot compare against software");
        return true;
    }

#ifdef SE050_BENCHMARK
    // dh2 destroys the private key it is given, so keep a copy for the benchmark below.
    uint8_t benchPrivate[32];
    memcpy(benchPrivate, testPrivate, 32);
#endif

    uint8_t sharedSoft[32];
    memcpy(sharedSoft, ourPublic, 32);
    if (!Curve25519::dh2(sharedSoft, testPrivate)) {
        LOG_WARN("SE050: software side of the comparison failed");
        return true;
    }

    if (memcmp(sharedChip, sharedSoft, 32) == 0) {
        LOG_INFO("SE050: ECDH matches software byte for byte - hardware X25519 is usable");
    } else {
        for (int i = 0; i < 32; i++)
            snprintf(&hex[i * 2], 3, "%02X", sharedChip[i]);
        LOG_ERROR("SE050: ECDH MISMATCH, chip=%s", hex);
        for (int i = 0; i < 32; i++)
            snprintf(&hex[i * 2], 3, "%02X", sharedSoft[i]);
        LOG_ERROR("SE050: ECDH MISMATCH, soft=%s", hex);
    }

#ifdef SE050_BENCHMARK
    // What one key agreement costs. Meshtastic runs a full ECDH per PKI packet, so
    // this number, not correctness, decides whether the chip can back the radio path.
    // Each round is a UserID session nested inside SCP03, which means AES-CMAC plus
    // AES-CBC over both the command and the response, all over I2C at 100 kHz.
    // Build-time opt-in (5 extra agreements, ~300ms, on every boot) - useful during
    // bring-up, not something a shipped device needs to redo every reset.
    constexpr int ROUNDS = 5;
    uint32_t best = UINT32_MAX, worst = 0, total = 0;
    int done = 0;
    for (int i = 0; i < ROUNDS; i++) {
        uint8_t tmp[32];
        uint32_t t0 = micros();
        bool ok = identityEcdh(testPublic, tmp);
        uint32_t dt = micros() - t0;
        if (!ok) {
            LOG_WARN("SE050: ECDH timing aborted, round %d failed", i + 1);
            break;
        }
        total += dt;
        best = min(best, dt);
        worst = max(worst, dt);
        done++;
    }

    uint8_t softShared[32];
    memcpy(softShared, ourPublic, 32);
    uint32_t t0 = micros();
    Curve25519::dh2(softShared, benchPrivate);
    uint32_t softUs = micros() - t0;

    if (done > 0)
        LOG_INFO("SE050: ECDH cost over %d rounds: min %u ms, avg %u ms, max %u ms | software %u ms (%ux)", done, best / 1000,
                 (total / done) / 1000, worst / 1000, softUs / 1000, softUs ? (total / done) / softUs : 0);
#endif

    return true;
}

#endif // HAS_SE050
