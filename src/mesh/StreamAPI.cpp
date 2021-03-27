#include "StreamAPI.h"
#include "configuration.h"

#define START1 0x94
#define START2 0xc3
#define HEADER_LEN 4

int32_t StreamAPI::runOnce()
{
    auto result = readStream();
    writeStream();
    checkConnectionTimeout();
    return result;
}

/**
 * Read any rx chars from the link and call handleToRadio
 */
int32_t StreamAPI::readStream()
{
    uint32_t now = millis();
    if (!stream->available()) {
        // Nothing available this time, if the computer has talked to us recently, poll often, otherwise let CPU sleep a long time
        bool recentRx = (now - lastRxMsec) < 2000;
        return recentRx ? 5 : 250;
    } else {
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
                uint32_t len = (rxBuf[2] << 8) + rxBuf[3]; // big endian 16 bit length follows framing

                if (ptr == HEADER_LEN) {
                    // we _just_ finished our 4 byte header, validate length now (note: a length of zero is a valid
                    // protobuf also)
                    if (len > MAX_TO_FROM_RADIO_SIZE)
                        rxPtr = 0; // length is bogus, restart search for framing
                }

                if (rxPtr != 0 && ptr + 1 == len + HEADER_LEN) {
                    rxPtr = 0; // start over again on the next packet

                    // If we didn't just fail the packet and we now have the right # of bytes, parse it
                    if (handleToRadio(rxBuf + HEADER_LEN, len))
                        return 0; // we want to be called again ASAP because we still have more work to do
                }
            }
        }

        // we had packets available this time, so assume we might have them next time also
        lastRxMsec = now;
        return 0;
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
            emitTxBuffer(len);
        } while (len);
    }
}

/**
 * Send the current txBuffer over our stream
 */
void StreamAPI::emitTxBuffer(size_t len)
{
    if (len != 0) {
        // DEBUG_MSG("emit tx %d\n", len);
        txBuf[0] = START1;
        txBuf[1] = START2;
        txBuf[2] = (len >> 8) & 0xff;
        txBuf[3] = len & 0xff;

        auto totalLen = len + HEADER_LEN;
        stream->write(txBuf, totalLen);
        /* for(size_t i = 0; i < totalLen; i++) {
            stream->write(txBuf[i]);
            // stream->flush();
        } */
    }
}

void StreamAPI::emitRebooted()
{
    // In case we send a FromRadio packet
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));
    fromRadioScratch.which_payloadVariant = FromRadio_rebooted_tag;
    fromRadioScratch.rebooted = true;

    // DEBUG_MSG("Emitting reboot packet for serial shell\n");
    emitTxBuffer(pb_encode_to_bytes(txBuf + HEADER_LEN, FromRadio_size, FromRadio_fields, &fromRadioScratch));
}