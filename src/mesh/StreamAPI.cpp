#include "StreamAPI.h"

#define START1 0x94
#define START2 0xc3
#define HEADER_LEN 4

void StreamAPI::loop()
{
    writeStream();
    readStream();
}

/**
 * Read any rx chars from the link and call handleToRadio
 */
void StreamAPI::readStream()
{
    while (stream->available()) { // Currently we never want to block
        uint8_t c = stream->read();

        // Use the read pointer for a little state machine, first look for framing, then length bytes, then payload
        size_t ptr = rxPtr++; // assume we will probably advance the rxPtr

        rxBuf[ptr] = c; // store all bytes (including framing)

        if (ptr == 0) { // looking for START1
            if (c != START1)
                rxPtr = 0;     // failed to find framing
        } else if (ptr == 1) { // looking for START2
            if (c != START2)
                rxPtr = 0;                             // failed to find framing
        } else if (ptr >= HEADER_LEN) {                // we have at least read our 4 byte framing
            uint16_t len = (rxBuf[2] << 8) + rxBuf[3]; // big endian 16 bit length follows framing

            if (ptr == HEADER_LEN) {
                // we _just_ finished our 4 byte header, validate length now (note: a length of zero is a valid
                // protobuf also)
                if (len > MAX_TO_FROM_RADIO_SIZE)
                    rxPtr = 0; // length is bogus, restart search for framing
            }

            if (rxPtr != 0 && ptr == len + HEADER_LEN) {
                // If we didn't just fail the packet and we now have the right # of bytes, parse it
                handleToRadio(rxBuf + HEADER_LEN, len);
            }
        }
    }
}

/**
 * call getFromRadio() and deliver encapsulated packets to the Stream
 */
void StreamAPI::writeStream()
{
    if (canWrite) {
        uint32_t len;
        do {
            // Send every packet we can
            len = getFromRadio(txBuf + HEADER_LEN);
            if (len != 0) {
                txBuf[0] = START1;
                txBuf[1] = START2;
                txBuf[2] = (len >> 8) & 0xff;
                txBuf[3] = len & 0xff;

                stream->write(txBuf, len + HEADER_LEN);
            }
        } while (len);
    }
}