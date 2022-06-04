#include "configuration.h"
#include "CryptoEngine.h"

void CryptoEngine::setKey(const CryptoKey &k)
{
    DEBUG_MSG("Using AES%d key!\n", k.length * 8);
    /* for(uint8_t i = 0; i < k.length; i++)
        DEBUG_MSG("%02x ", k.bytes[i]);
    DEBUG_MSG("\n"); */

    key = k;
}

/**
 * Encrypt a packet
 *
 * @param bytes is updated in place
 */
void CryptoEngine::encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes)
{
    DEBUG_MSG("WARNING: noop encryption!\n");
}

void CryptoEngine::decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes)
{
    DEBUG_MSG("WARNING: noop decryption!\n");
}

// Usage:
//     hexDump(desc, addr, len, perLine);
//         desc:    if non-NULL, printed as a description before hex dump.
//         addr:    the address to start dumping from.
//         len:     the number of bytes to dump.
//         perLine: number of bytes on each output line.

void CryptoEngine::hexDump (const char * desc, const void * addr, const int len, int perLine)
{
    // Silently ignore silly per-line values.

    if (perLine < 4 || perLine > 64) perLine = 16;

    int i;
    unsigned char buff[perLine+1];
    const unsigned char * pc = (const unsigned char *)addr;

    // Output description if given.

    if (desc != NULL) DEBUG_MSG ("%s:\n", desc);

    // Length checks.

    if (len == 0) {
        DEBUG_MSG("  ZERO LENGTH\n");
        return;
    }
    if (len < 0) {
        DEBUG_MSG("  NEGATIVE LENGTH: %d\n", len);
        return;
    }

    // Process every byte in the data.

    for (i = 0; i < len; i++) {
        // Multiple of perLine means new or first line (with line offset).

        if ((i % perLine) == 0) {
            // Only print previous-line ASCII buffer for lines beyond first.

            if (i != 0) DEBUG_MSG ("  %s\n", buff);

            // Output the offset of current line.

            DEBUG_MSG ("  %04x ", i);
        }

        // Now the hex code for the specific character.

        DEBUG_MSG (" %02x", pc[i]);

        // And buffer a printable ASCII character for later.

        if ((pc[i] < 0x20) || (pc[i] > 0x7e)) // isprint() may be better.
            buff[i % perLine] = '.';
        else
            buff[i % perLine] = pc[i];
        buff[(i % perLine) + 1] = '\0';
    }

    // Pad out last line if not exactly perLine characters.

    while ((i % perLine) != 0) {
        DEBUG_MSG ("   ");
        i++;
    }

    // And print the final ASCII buffer.

    DEBUG_MSG ("  %s\n", buff);
}

/**
 * Init our 128 bit nonce for a new packet
 */
void CryptoEngine::initNonce(uint32_t fromNode, uint64_t packetId)
{
    memset(nonce, 0, sizeof(nonce));

    // use memcpy to avoid breaking strict-aliasing
    memcpy(nonce, &packetId, sizeof(uint64_t));
    memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t));
    //*((uint64_t *)&nonce[0]) = packetId;
    //*((uint32_t *)&nonce[8]) = fromNode;
}